#define OCL_PLATFORM 1
#define OCL_DEVICE 0

#define DRAGON "res/dragon.obj"
#define CUBE "res/cube_example.obj"
#define HUMAN "res/human.obj"

#include "ocl_boiler.h"
#include <iostream>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/gtx/string_cast.hpp>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

#define OCL_FILENAME "src/meshsmooth.ocl"

size_t preferred_wg_init;

std::vector< glm::vec3 > obj_vertexArray;
float* vertexArray;

void loadOBJFile(std::string path){

	std::vector< unsigned int > vertexIndices;

	FILE * file = fopen(path.c_str(), "r");
	if( file == NULL ){
		printf("Impossible to open the file !\n");
		return;
	}
	while( 1 ){
		char lineHeader[128];
		// read the first word of the line
		int res = fscanf(file, "%s", lineHeader);
		if (res == EOF) break; // EOF = End Of File. Quit the loop.
		// else : parse lineHeader
		if ( strcmp( lineHeader, "v" ) == 0 ){
			glm::vec3 vertex;
			fscanf(file, "%f %f %f\n", &vertex.x, &vertex.y, &vertex.z );
			obj_vertexArray.push_back(vertex);
		} else if ( strcmp( lineHeader, "f" ) == 0 ){
			unsigned int vertexIndex[3], uvIndex[3], normalIndex[3];
			int matches = fscanf(file, "%d//%d %d//%d %d//%d\n", &vertexIndex[0], &normalIndex[0], &vertexIndex[1], &normalIndex[1], &vertexIndex[2], &normalIndex[2] );
			if (matches != 6){
				printf("File can't be read by our simple parser : ( Try exporting with other options\n");
				return;
			}
			vertexIndices.push_back(vertexIndex[0]);
			vertexIndices.push_back(vertexIndex[1]);
			vertexIndices.push_back(vertexIndex[2]);
		}
	}


	int i=0;
	vertexArray = new float[obj_vertexArray.size()*4];
	for(glm::vec3 vertex : obj_vertexArray){
		vertexArray[i++] = vertex.x;
		vertexArray[i++] = vertex.y;
		vertexArray[i++] = vertex.z;
		vertexArray[i++] = 0.0f;
	}

	
	std::vector< unsigned int >* adjacents = new std::vector< unsigned int >[obj_vertexArray.size()];

	for(int i=0; i<vertexIndices.size(); i+=3){
		unsigned int vertexID1 = vertexIndices[i] - 1;
		unsigned int vertexID2 = vertexIndices[i+1] - 1;
		unsigned int vertexID3 = vertexIndices[i+2] - 1;

		//glm::vec3 vertex1 = obj_vertexArray[ vertexID1 ];
		//glm::vec3 vertex2 = obj_vertexArray[ vertexID2 ];
		//glm::vec3 vertex3 = obj_vertexArray[ vertexID3 ];

		std::vector< unsigned int >* adjacent1 = &adjacents[vertexID1];
		std::vector< unsigned int >* adjacent2 = &adjacents[vertexID2];
		std::vector< unsigned int >* adjacent3 = &adjacents[vertexID3];
		
		if (std::find(adjacent1->begin(), adjacent1->end(), vertexID2) == adjacent1->end())
			adjacent1->push_back(vertexID2);
		if (std::find(adjacent1->begin(), adjacent1->end(), vertexID3) == adjacent1->end())
			adjacent1->push_back(vertexID3);

		if (std::find(adjacent2->begin(), adjacent2->end(), vertexID1) == adjacent2->end())
			adjacent2->push_back(vertexID1);
		if (std::find(adjacent2->begin(), adjacent2->end(), vertexID3) == adjacent2->end())
			adjacent2->push_back(vertexID3);

		if (std::find(adjacent3->begin(), adjacent3->end(), vertexID1) == adjacent3->end())
			adjacent3->push_back(vertexID1);
		if (std::find(adjacent3->begin(), adjacent3->end(), vertexID2) == adjacent3->end())
			adjacent3->push_back(vertexID2);
	}



	#if 0
	for(int i=0; i<obj_vertexArray.size(); i++) {
		std::cout << "adiacenti di " << i << " " << glm::to_string(obj_vertexArray[i]) << " => " << std::endl;

		for(unsigned int adj : adjacents[i])  std::cout << glm::to_string(obj_vertexArray[adj]) << std::endl;	
		std::cout << std::endl;	

	}
	#endif
}

cl_event init(cl_command_queue que, cl_kernel init_k, cl_mem cl_vertexArray, cl_mem cl_vertexResult, cl_int nels) {
	size_t gws[] = { round_mul_up(nels, preferred_wg_init) };
	cl_event init_evt;
	cl_int err;

	printf("init gws: %d | %zu => %zu\n", nels, preferred_wg_init, gws[0]);

	// Setting arguments
	err = clSetKernelArg(init_k, 0, sizeof(cl_vertexArray), &cl_vertexArray);
	ocl_check(err, "set init arg 0");
	err = clSetKernelArg(init_k, 1, sizeof(cl_vertexResult), &cl_vertexResult);
	ocl_check(err, "set init arg 1");
	err = clSetKernelArg(init_k, 2, sizeof(nels), &nels);
	ocl_check(err, "set init arg 2");

	err = clEnqueueNDRangeKernel(que, init_k,
		1, NULL, gws, NULL, /* griglia di lancio */
		0, NULL, /* waiting list */
		&init_evt);
	ocl_check(err, "enqueue kernel init");
	return init_evt;
}

int main(int argc, char *argv[]) {

	loadOBJFile(HUMAN);
	int nels = obj_vertexArray.size();
	const size_t memsize = 4*nels*sizeof(float);

	/* Hic sunt leones */
	cl_int err;
	cl_platform_id p = select_platform();
	cl_device_id deviceID = select_device(p);
	cl_context context = create_context(p, deviceID);
	cl_command_queue que = create_queue(context, deviceID);
	cl_program prog = create_program(OCL_FILENAME, context, deviceID);

	cl_mem cl_vertexArray = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, memsize, vertexArray, &err);
	cl_mem cl_vertexResult = clCreateBuffer(context, CL_MEM_READ_WRITE, memsize, NULL, &err);

	/* Extract kernels */
	cl_kernel init_k = clCreateKernel(prog, "init", &err);
	ocl_check(err, "create kernel init");

	// Set preferred_wg size from device info
	err = clGetKernelWorkGroupInfo(init_k, deviceID, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(preferred_wg_init), &preferred_wg_init, NULL);
	cl_event init_evt = init(que, init_k, cl_vertexArray, cl_vertexResult, nels);

	err = clWaitForEvents(1, &init_evt);
	ocl_check(err, "clWaitForEvents");

	printf("init time:\t%gms\t%gGB/s\n", runtime_ms(init_evt), (2.0*memsize)/runtime_ns(init_evt));

	return 0;
}
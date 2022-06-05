#pragma once
#include <stddef.h> // size_t

enum DataType { DataType_Buffer = 0, DataType_Count };
enum DataAccess { DataAccess_Read = 0, DataAccess_Write, DataAccess_Count };
enum DataSource { DataSource_File = 0, DataSource_Directory, DataSource_Memory, DataSource_Count };
enum Access { Access_GPU_Read = 0, Access_GPU_Write, Access_GPU_ReadWrite, Access_CPU_Read, Access_CPU_Write, Access_Count };

struct Parameters
{ 
	size_t poolSizes[Access_Count];
	int iterations;
};

struct Data
{
	const char *name;
	const char *path;
	size_t size;
	DataSource source;
	DataAccess access;
	DataType type;
};

struct Program
{
	const char *name;
	const char *path;
	size_t dispatch[3];
};

struct Description
{
	Parameters parameters;
	Data *dataList;
	Program *programList;
	int dataCount;
	int programCount;
	void *data;
};

const Description *descCreateFromFile(const char *_path);
const Description *descCreateFromMemory(const char *_buffer);
void descDestroy(const Description *_description);


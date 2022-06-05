#include "desc.h"

#include <string.h>
#include <malloc.h>
#include "cJSON.h"


const size_t SAFE_ALIGNMENT = 1024;
const char STRING_ANONYMOUS[] = "Anonymous";


static size_t alignSize(size_t _size, size_t _alignment = SAFE_ALIGNMENT)
{
	size_t asize = _size & (~(_alignment - 1));
	return (asize == _size) ? asize : asize + _alignment;
}


static bool descParseParam(Description *_description, const cJSON *_param)
{
	bool result = true;
	const cJSON *iter = cJSON_GetObjectItem(_param, "iterations");
	if(iter && cJSON_IsNumber(iter)) _description->parameters.iterations = cJSON_GetNumberValue(iter);
	else
	{
		printf("[Warning] JSON param.iterations is not provided, default (8)\n");
		_description->parameters.iterations = 8;
	}

	// Initialize the memory pools to minimum SAFE_ALIGNMENT
	for(int i = 0; i < Access_Count; ++i) _description->parameters.poolSizes[i] = SAFE_ALIGNMENT;

	return result;
}


static bool descParseData(Description *_description, const cJSON *_data)
{
	bool result = true;

	int count = cJSON_GetArraySize(_data);
	if(count == 0) { printf("[Warning] JSON data[] is empty, your compute won't output any results\n"); }

	_description->dataList = (Data *) malloc(sizeof(Data) * count);
	_description->dataCount = count;

	for(int i = 0; i < count; ++i)
	{
		const cJSON *item = cJSON_GetArrayItem(_data, i);
		const cJSON *size = cJSON_GetObjectItem(item, "size");
		const cJSON *source = cJSON_GetObjectItem(item, "source");
		const cJSON *type = cJSON_GetObjectItem(item, "type");
		const cJSON *access = cJSON_GetObjectItem(item, "access");
		const cJSON *path = cJSON_GetObjectItem(item, "path");
		const cJSON *name = cJSON_GetObjectItem(item, "name");

		// [Mandatory] size parsing
		if(size && cJSON_IsNumber(size)) _description->dataList[i].size = cJSON_GetNumberValue(size);
		else { printf("[Error] JSON data[%d].size is not provided, it can't be deduced\n", i); result = false; }

		// [Mandatory] source parsing
		if(source && cJSON_IsString(source))
		{
			const char *src = cJSON_GetStringValue(source);
			if(strcmp(src, "file") == 0) _description->dataList[i].source = DataSource_File;
			else if(strcmp(src, "directory") == 0) _description->dataList[i].source = DataSource_Directory;
			else if(strcmp(src, "memory") == 0) _description->dataList[i].source = DataSource_Memory;
			else { printf("[Error] JSON data[%d].source=%s, doesn't match any known value\n", i, src); result = false; }
		}
		else { printf("[Error] JSON data[%d].source is not provided, it can't be deduced\n", i); result = false; }

		// [Mandatory] type parsing
		if(type && cJSON_IsString(type))
		{
			const char *tp = cJSON_GetStringValue(type);
			if(strcmp(tp, "buffer") == 0) _description->dataList[i].type = DataType_Buffer;
			else { printf("[Error] JSON data[%d].type=%s doesn't match any known value\n", i, tp); result = false; }
		}
		else { printf("[Error] JSON data[%d].type is not provided, it can't be deduced\n", i); result = false; }

		// [Mandatory/Optional] access parsing
		if(access && cJSON_IsString(access))
		{
			const char *acc = cJSON_GetStringValue(access);
			if(_description->dataList[i].source != DataSource_Memory)
			{
				if(strcmp(acc, "read") == 0) _description->dataList[i].access = DataAccess_Read;
				else if(strcmp(acc, "write") == 0) _description->dataList[i].access = DataAccess_Write;
				else
				{
					printf("[Error] JSON data[%d],access=%s doesn't match any known value\n", i, acc);
					result = false; 
				}
			}
			else { printf("[Warning] JSON data[%d].access=%s will be ignored since source = \"memory\"\n", i, acc); }
		}
		else if(_description->dataList[i].source != DataSource_Memory)
		{
			printf("[Error] JSON data[%d].access is not provided, it can't be deduced\n", i);
			result = false;
		}

		// [Mandatory/Optional] path parsing
		if(path && cJSON_IsString(path))
		{
			const char *pth = cJSON_GetStringValue(path);
			bool notMemory = _description->dataList[i].source != DataSource_Memory;
			bool notFileWrite = _description->dataList[i].source != DataSource_File && 
								_description->dataList[i].access != DataAccess_Write;

			if(notMemory)
			{
				_description->dataList[i].path = pth;

				if(notFileWrite)
				{
					FILE *fp = fopen(pth, "rb");
					if(fp) { fclose(fp); }
					else { printf("[Error] data[%d].path=%s can't be found on disk\n", i, pth); result = false; }
				}
			}
		}
		else if(_description->dataList[i].source != DataSource_Memory)
		{
			printf("[Error] JSON data[%d].path is not provided, path is mandatory\n", i);
			result = false;
		}

		// [Optional] name parsing
		if(name && cJSON_IsString(name)) _description->dataList[i].name = cJSON_GetStringValue(name);
		else
		{
			printf("[Warning] JSON data[%d].name is not provided, default (\"Anonymous\")\n", i);
			_description->dataList[i].name = STRING_ANONYMOUS;
		}

		// Accumulate memory required from the different pools for this data item
		const Data *dataItem = _description->dataList + i;		
		size_t *poolSizes = _description->parameters.poolSizes;
		size_t asize = alignSize(dataItem->size);
		switch(dataItem->source)
		{
			case DataSource_File:
				if(dataItem->access == DataAccess_Read)
				{
					poolSizes[Access_CPU_Write] += asize;
					poolSizes[Access_GPU_Read] += asize;
				}
				else if(dataItem->access == DataAccess_Write)
				{
					poolSizes[Access_CPU_Read] += asize;
					poolSizes[Access_GPU_Write] += asize;
				}
				break;
			case DataSource_Directory:
				if(dataItem->access == DataAccess_Read)
				{
					poolSizes[Access_CPU_Write] += 2*asize;
					poolSizes[Access_GPU_Read] += 2*asize;
				}
				else if(dataItem->access == DataAccess_Write)
				{
					poolSizes[Access_CPU_Read] += 2*asize;
					poolSizes[Access_GPU_Write] += 2*asize;
				}
				break;
			case DataSource_Memory:
				poolSizes[Access_GPU_ReadWrite] += asize;
				break;
		}
	}

	return result;
}


static bool descParseProgram(Description *_description, const cJSON *_program)
{
	bool result = true;

	int count = cJSON_GetArraySize(_program);
	if(count == 0) { printf("[Warning] JSON program[] is empty, your compute won't process any data\n"); }

	_description->programList = (Program *) malloc(sizeof(Program) * count);
	_description->programCount = count;

	for(int i = 0; i < count; ++i)
	{
		const cJSON *item = cJSON_GetArrayItem(_program, i);
		const cJSON *dispatch = cJSON_GetObjectItem(item, "dispatch");
		const cJSON *path = cJSON_GetObjectItem(item, "path");
		const cJSON *name = cJSON_GetObjectItem(item, "name");

		// [Mandatory] dispatch parsing
		if(dispatch && cJSON_IsArray(dispatch) && (cJSON_GetArraySize(dispatch) == 3))
		{
			const cJSON *dx = cJSON_GetArrayItem(dispatch, 0);
			const cJSON *dy = cJSON_GetArrayItem(dispatch, 1);
			const cJSON *dz = cJSON_GetArrayItem(dispatch, 2);
			if(dx && dy && dz && cJSON_IsNumber(dx) && cJSON_IsNumber(dy) && cJSON_IsNumber(dy))
			{
				_description->programList[i].dispatch[0] = cJSON_GetNumberValue(dx);
				_description->programList[i].dispatch[1] = cJSON_GetNumberValue(dy);
				_description->programList[i].dispatch[2] = cJSON_GetNumberValue(dz);

				size_t total = _description->programList[i].dispatch[0];
				total *= _description->programList[i].dispatch[1];
				total *= _description->programList[i].dispatch[2];
				if(total >= 1000000)
					printf("[Warning] Attempting to dispatch %lu thread groups, " \
							"GPU driver might trigger a timeout!\n", total);
			}
			else { printf("[Error] program[%d].dispatch[3] is invalid, expecting 3 integers\n", i); result = false; }
		}
		else { printf("[Error] program[%d].dispatch[3] is not provided, it can't be deduced\n", i); result = false; }

		// [Mandatory] path parsing
		if(path && cJSON_IsString(path))
		{
			const char *pth = cJSON_GetStringValue(path);
			FILE *fp = fopen(pth, "rb");
			if(fp) { _description->programList[i].path = pth; fclose(fp); }
			else { printf("[Error] program[%d].path=%s can't be found on disk\n", i, pth); result = false; }
		}
		else { printf("[Error] program[%d].path is not provided, path is mandatory\n", i); result = false; }

		// [Optional] name parsing
		if(name && cJSON_IsString(name)) _description->programList[i].name = cJSON_GetStringValue(name);
		else
		{
			printf("[Warning] JSON program[%d].name is not provided, default (\"Anonymous\")\n", i);
			_description->dataList[i].name = STRING_ANONYMOUS;
		}
	}

	return result;
}


static void descInfo(const Description *_description)
{
	if(_description)
	{
		const size_t SIZE_IN_MIB = 1024*1024;
		const size_t *poolSizes = _description->parameters.poolSizes;
		printf("[Info] Iterations: %d\n", _description->parameters.iterations);
		printf("[Info] Data count: %d\n", _description->dataCount);
		printf("[Info] Program count: %d\n", _description->programCount);
		printf("[Info] Memory GPU Read: %lu MiB\n", poolSizes[Access_GPU_Read] / SIZE_IN_MIB);
		printf("[Info] Memory GPU Write: %lu MiB\n", poolSizes[Access_GPU_Write] / SIZE_IN_MIB);
		printf("[Info] Memory GPU ReadWrite: %lu MiB\n", poolSizes[Access_GPU_ReadWrite] / SIZE_IN_MIB);
		printf("[Info] Memory CPU Read: %lu MiB\n", poolSizes[Access_CPU_Read] / SIZE_IN_MIB);
		printf("[Info] Memory CPU Write: %lu MiB\n", poolSizes[Access_CPU_Write] / SIZE_IN_MIB);
	}
	else
	{
		printf("[Error] JSON compute description is invalid\n");
	}
}


const Description *descCreateFromMemory(const char *_buffer)
{
	// JSON library version
	printf("[Info] cJSON version: %s\n", cJSON_Version());
	printf("[Info] JSON begin parsing...\n");

	// JSON valid file check
	cJSON *json = cJSON_Parse(_buffer);
	if(!json) { printf("[Error] JSON format parsing error\n"); return 0; }

	// Basic JSON compute file format sanity checks
	const cJSON *param = cJSON_GetObjectItem(json, "param");
	const cJSON *data = cJSON_GetObjectItem(json, "data");
	const cJSON *program = cJSON_GetObjectItem(json, "program");

	if(!param && !cJSON_IsObject(param))
	{ 
		printf("[Error] Mandatory JSON compute \"param\" object is not present\n");
		cJSON_Delete(json);
		return 0;
	}

	if(!data && !cJSON_IsArray(data))
	{
		printf("[Error] Mandatory JSON compute \"data\" array is not present\n");
		cJSON_Delete(json);
		return 0;
	}

	if(!program && !cJSON_IsArray(program))
	{
		printf("[Error] Mandatory JSON compute \"program\" array is not present\n");
		cJSON_Delete(json);
		return 0;
	}

	// It looks like a valid JSON compute file, let's allocate the parsing structure
	Description *description = (Description *) malloc(sizeof(Description));
	memset(description, 0, sizeof(Description));
	description->data = json;

	bool bparam = descParseParam(description, param);
	bool bdata = descParseData(description, data);
	bool bprogram = descParseProgram(description, program);

	bool success = bparam && bdata && bprogram;
	if(!success) { descDestroy(description); description = 0; }

	descInfo(description);
	printf("[Info] JSON end parsing...\n");

	return description;
}


const Description *descCreateFromFile(const char *_path)
{
	const Description *description = 0;

	FILE *fp = fopen(_path, "r");
	if(fp)
	{
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		char *buffer = (char *) malloc(size + 1);
		fread(buffer, 1, size, fp); buffer[size] = 0;
		description = descCreateFromMemory(buffer);

		free(buffer);
		fclose(fp);
	}
	else printf("[Error] JSON file not found %s\n", _path);

	return description;
}


void descDestroy(const Description *_description)
{
	if(_description)
	{
		cJSON *json = (cJSON *) _description->data;
		cJSON_Delete(json);

		free(_description->dataList);
		free(_description->programList);
		free((void*)_description);
	}
}



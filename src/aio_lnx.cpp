#include "aio.h"
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <libaio.h>
#include <sys/stat.h>

struct AIOCmdBuffer
{
	iocb **commands;
	iocb *pool;
	int count;
};

struct AIO
{
	io_context_t context;
	io_event *events;
	AIOCmdBuffer *pending;
	size_t size;
};

AIO *aioCreate(size_t _size)
{
	AIO *aio = (AIO*) malloc(sizeof(AIO));

	aio->size = _size;
	aio->pending = 0;
	aio->events = (io_event*) malloc(sizeof(io_event) * _size);
	memset(&aio->context, 0, sizeof(io_context_t));
	long result = io_setup(aio->size, &aio->context);

	return aio;
}

void aioDestroy(AIO *_aio)
{
	aioWaitIdle(_aio);
	free(_aio->events);
	io_destroy(_aio->context);

	free(_aio);
}

bool aioScanDirectory(const char *_dir, aioEntryCallback _callback, void *_data)
{
	bool ret = false;

	DIR *dir = opendir(_dir);
	if(dir != 0)
	{
		dirent *entry = readdir(dir);
		while(entry != 0)
		{
			if(entry->d_type == DT_DIR)
			{
				_callback(entry->d_name, true, 0, _data);
			}
			else if(entry->d_type == DT_REG)
			{
				struct stat st;
				char path[512];
				strcpy(path, _dir); strcat(path, "/"); strcat(path, entry->d_name);

				lstat(path, &st);
				_callback(entry->d_name, false, st.st_size, _data);
			}
			entry = readdir(dir);
		}

		closedir(dir);
		ret = true;
	}
	
	return ret;
}

AIOCmdBuffer *aioAllocCmdBuffer(AIO *_aio)
{
	AIOCmdBuffer *cmd = (AIOCmdBuffer *)malloc(sizeof(AIO));
	cmd->pool = (iocb *) malloc(sizeof(iocb) * _aio->size);
	cmd->commands = (iocb **) malloc(sizeof(iocb*) * _aio->size);
	cmd->count = 0;

	for(int i = 0; i < _aio->size; ++i) cmd->commands[i] = cmd->pool + i;

	return cmd;
}

void aioFreeCmdBuffer(AIOCmdBuffer *_cmdBuffer)
{
	free(_cmdBuffer->commands);
	free(_cmdBuffer->pool);
	free(_cmdBuffer);
}

void aioBeginCmdBuffer(AIOCmdBuffer *_cmdBuffer)
{
	_cmdBuffer->count = 0;
}

void aioEndCmdBuffer(AIOCmdBuffer *_cmdBuffer)
{

}

bool aioCmdRead(AIOCmdBuffer *_cmdBuffer, void *_buffer, const char *_file, size_t _size, size_t _offset)
{
	bool ret = false;

	int fd = open(_file, O_NONBLOCK | O_RDONLY /*| O_DIRECT*/);
	if(fd > 0)
	{
		io_prep_pread(_cmdBuffer->commands[_cmdBuffer->count], fd, _buffer, _size, _offset);
		++_cmdBuffer->count;
		ret = true;
	}

	return ret;
}

bool aioCmdWrite(AIOCmdBuffer *_cmdBuffer, void *_buffer, const char *_file, size_t _size, size_t _offset)
{
	bool ret = false;

	int fd = open(_file, O_NONBLOCK | O_WRONLY | O_CREAT | O_TRUNC /*| O_DIRECT*/, 0644);
	if(fd > 0)
	{
		io_prep_pwrite(_cmdBuffer->commands[_cmdBuffer->count], fd, _buffer, _size, _offset);
		++_cmdBuffer->count;
		ret = true;
	}

	return ret;
}

bool aioSubmitCmdBuffer(AIO *_aio, AIOCmdBuffer *_cmdBuffer)
{
	bool ret = false;

	if(_aio->pending == 0)
	{
		int num = io_submit(_aio->context, _cmdBuffer->count, _cmdBuffer->commands);
		_aio->pending = _cmdBuffer;
		ret = (num == _cmdBuffer->count);
	}

	return ret;
}

bool aioWaitIdle(AIO *_aio)
{
	bool ret = true;

	if(_aio->pending != 0)
	{
		int count = _aio->pending->count;
		
		// Wait an infinite time
		int num = io_getevents(_aio->context, count, count, _aio->events, NULL);

		// Close all file descriptors
		for(int i = 0; i < _aio->pending->count; ++i)
		{
			int res;
			//res = fsync(_aio->pending->commands[i]->aio_fildes);
			res = close(_aio->pending->commands[i]->aio_fildes);
		}
		
		_aio->pending = 0;

		ret = (num == count);
	}

	return ret;
}


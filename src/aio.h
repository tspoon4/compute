#pragma once
#include <stddef.h> // size_t

struct AIO;
struct AIOCmdBuffer;

AIO *aioCreate(size_t _size);
void aioDestroy(AIO *_aio);

typedef void (*aioEntryCallback)(const char *_name, bool _directory, size_t _size, void *_data);
bool aioScanDirectory(const char *_dir, aioEntryCallback _callback, void *_data);

AIOCmdBuffer *aioAllocCmdBuffer(AIO *_aio);
void aioFreeCmdBuffer(AIOCmdBuffer *_cmdBuffer);
void aioBeginCmdBuffer(AIOCmdBuffer *_cmdBuffer);
void aioEndCmdBuffer(AIOCmdBuffer *_cmdBuffer);

bool aioCmdRead(AIOCmdBuffer *_cmdBuffer, void *_buffer, const char *_file, size_t _size, size_t _offset = 0);
bool aioCmdWrite(AIOCmdBuffer *_cmdBuffer, void *_buffer, const char *_file, size_t _size, size_t _offset = 0);

bool aioSubmitCmdBuffer(AIO *_aio, AIOCmdBuffer *_cmdBuffer);
bool aioWaitIdle(AIO *_aio);


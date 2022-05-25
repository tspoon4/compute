#pragma once

struct Clock { long data1, data2; };

void clockGetTime(Clock *_time);
double clockDeltaTime(const Clock *_start, const Clock *_stop);


#include "clock.h"
#include <time.h>

void clockGetTime(Clock *_time)
{
	timespec *time = (timespec *) _time;
	clock_gettime(CLOCK_REALTIME, time);
}

double clockDeltaTime(const Clock *_start, const Clock *_stop)
{
	timespec *start = (timespec *)_start;
	timespec *stop = (timespec *)_stop;

	long delta = (stop->tv_sec - start->tv_sec) * 1000000000 + (stop->tv_nsec - start->tv_nsec);
	return 1.0e-9 * delta;
}


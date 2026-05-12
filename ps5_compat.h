#pragma once
#include <pthread_np.h>
/* FreeBSD uses pthread_set_name_np; map it to the Linux/glibc name */
#define pthread_setname_np(t, n) pthread_set_name_np((t), (n))

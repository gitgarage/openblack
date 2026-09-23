// newlib for the Switch has memalign but no posix_memalign, which bimg's astc encoder calls.
#include <errno.h>
#include <malloc.h>
#include <stddef.h>

int posix_memalign(void **out, size_t alignment, size_t size)
{
	if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
		return EINVAL;
	void *p = memalign(alignment, size);
	if (!p)
		return ENOMEM;
	*out = p;
	return 0;
}

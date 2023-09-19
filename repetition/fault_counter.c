#include "..\common.c"

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s [page count]\n", argv[0]);
		exit(1);
	}

	os_metrics_init();

	U64 page_count = atol(argv[1]);
	U64 page_size = 4 * 1024;
	U64 buffer_size = page_count * page_size; 

	printf("Page Count, Touch Count, Fault Count, Extra Faults\n");

	for (U64 touch_count = 0; touch_count <= page_count; ++touch_count) {
		// allocate memory		
		U8 *buffer = VirtualAlloc(0, buffer_size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
		if (!buffer) {
			fprintf(stderr, "Unable to allocate memory\n");
			continue;
		}

		// touch specified number of pages
		U64 touch_size = touch_count * page_size;
		U64 start_fault_count = os_process_page_fault_count();
		for (U64 i=0; i<touch_size; ++i) {
			// buffer[i] = 0x69;                     // forward
			buffer[(buffer_size - 1) - i] = 0x69; // reverse
		}
		U64 end_fault_count = os_process_page_fault_count();

		// print page fault counts
		U64 fault_count = end_fault_count - start_fault_count;
		U64 extra_faults = fault_count - touch_count;
		printf("%llu, %llu, %llu, %llu\n", page_count, touch_count, fault_count, extra_faults);

		// free memory
		VirtualFree(buffer, 0, MEM_RELEASE);
	}

	return 0;
}

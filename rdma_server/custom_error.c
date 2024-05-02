#include <stdint.h>
#include <stdio.h>
#include <time.h>

// negative funcCodes generally mean RDMA errors
uint32_t goError(int16_t funcCode, int16_t minioCode) {
  // Ensure code2 is treated as unsigned (uint16_t)
  uint16_t c2 = minioCode & 0xFFFF;
  // Shift code1 16 bits to the left to occupy the most significant bits
  uint32_t c1 = funcCode << 16;
  // Combine the shifted code1 and code2_uint16
  uint32_t combined = c1 | c2;
  return combined;
}

uint32_t makeError(int8_t funcCode, int8_t minioCode, int8_t extra1, int8_t extra2) {
  uint8_t uint8_data1 = funcCode & 0xFF;
  uint8_t uint8_data2 = minioCode & 0xFF;
  uint8_t uint8_data3 = extra1 & 0xFF;
  uint8_t uint8_data4 = extra2 & 0xFF;

  return (uint8_data4 << 24) | (uint8_data3 << 16) | (uint8_data2 << 8) | uint8_data1;
}

void printBits(uint32_t value) {
  // Loop through each bit (32 in total)
  for (int i = 31; i >= 0; i--) {
    // Isolate the current bit using a bit mask
    uint32_t mask = 1 << i;

    // Check if the bit is set using bitwise AND
    int bitValue = (value & mask) ? 1 : 0;

    // Print the bit value with a space
    printf("%d ", bitValue);
  }
  printf("\n");
}

// int main() {
//   uint32_t x = goError(10, 2);
//   printf("error %ul\n", x);
//   printBits(x);
// }

uint64_t timestampDiff(struct timespec *t1, struct timespec *t2) {
  uint64_t difference_ms;

  difference_ms = (t1->tv_sec - t2->tv_sec) * 1000;

  // Calculate difference in nanoseconds (considering overflow)
  if (t1->tv_nsec >= t2->tv_nsec) {
    difference_ms += (t1->tv_nsec - t2->tv_nsec) / 1000000;
  } else {
    // Handle potential overflow (t2 might be larger)
    difference_ms += ((t1->tv_nsec + 1000000000) - t2->tv_nsec) / 1000000;
    difference_ms--; // Adjust for the borrowed second
  }

  printf("loop diff: %lu", difference_ms);
  return difference_ms;
}

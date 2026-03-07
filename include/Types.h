#pragma once

#include <stdint.h>

/* signed types */
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

/* unsigned types */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* floating point types */
typedef float f32;
typedef double f64;

/* boolean types */
typedef u8 b8;
typedef u16 b16;
typedef u32 b32;
typedef u64 b64;

/* memory size macros */
#define B(x) (x)
#define KB(x) ((x) << 10)
#define MB(x) ((x) << 20)
#define GB(x) ((x) << 30)
#define TB(x) (((u64)x) << 40)

/* signed constants */
static const i8 MAX_I8 = 127;
static const i16 MAX_I16 = 32767;
static const i32 MAX_I32 = 2147483647;
static const i64 MAX_I64 = 9223372036854775807;
static const i8 MIN_I8 = -127 - 1;
static const i16 MIN_I16 = -32767 - 1;
static const i32 MIN_I32 = -2147483647 - 1;
static const i64 MIN_I64 = -9223372036854775807 - 1;

/* unsigned constants */
static const u8 MAX_U8 = 0xFF;				   // 255
static const u16 MAX_U16 = 0xFFFF;			   // 65535
static const u32 MAX_U32 = 0xFFFFFFFF;		   // 4294967295
static const u64 MAX_U64 = 0xFFFFFFFFFFFFFFFF; // 18446744073709551615

/* floating point constants */
static const f32 MAX_F32 = 3.402823466e+38f;
static const f32 MIN_F32 = -3.402823466e+38f;
static const f32 SMALLEST_POSITIVE_F32 = 1.1754943508e-38f;
static const f32 EPSILON_F32 = 5.96046448e-8f;
static const f32 PI_F32 = 3.14159265359f;
static const f32 HALF_PI_F32 = 1.5707963267f;
static const f64 MAX_F64 = 1.79769313486231e+308;
static const f64 MIN_F64 = -1.79769313486231e+308;
static const f64 SMALLEST_POSITIVE_F64 = 4.94065645841247e-324;
static const f64 EPSILON_F64 = 1.11022302462515650e-16;
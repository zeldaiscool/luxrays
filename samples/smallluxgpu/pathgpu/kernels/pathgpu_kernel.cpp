#include "pathgpu/kernels/kernels.h"
std::string luxrays::KernelSource_PathGPU = 
"/***************************************************************************\n"
" *   Copyright (C) 1998-2010 by authors (see AUTHORS.txt )                 *\n"
" *                                                                         *\n"
" *   This file is part of LuxRays.                                         *\n"
" *                                                                         *\n"
" *   LuxRays is free software; you can redistribute it and/or modify       *\n"
" *   it under the terms of the GNU General Public License as published by  *\n"
" *   the Free Software Foundation; either version 3 of the License, or     *\n"
" *   (at your option) any later version.                                   *\n"
" *                                                                         *\n"
" *   LuxRays is distributed in the hope that it will be useful,            *\n"
" *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *\n"
" *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *\n"
" *   GNU General Public License for more details.                          *\n"
" *                                                                         *\n"
" *   You should have received a copy of the GNU General Public License     *\n"
" *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *\n"
" *                                                                         *\n"
" *   LuxRays website: http://www.luxrender.net                             *\n"
" ***************************************************************************/\n"
"\n"
"//#pragma OPENCL EXTENSION cl_amd_printf : enable\n"
"\n"
"// List of symbols defined at compile time:\n"
"//  PARAM_PATH_COUNT\n"
"//  PARAM_IMAGE_WIDTH\n"
"//  PARAM_IMAGE_HEIGHT\n"
"//  PARAM_STARTLINE\n"
"//  PARAM_RASTER2CAMERA_IJ (Matrix4x4)\n"
"//  PARAM_RAY_EPSILON\n"
"//  PARAM_CLIP_YON\n"
"//  PARAM_CLIP_HITHER\n"
"//  PARAM_CAMERA2WORLD_IJ (Matrix4x4)\n"
"//  PARAM_SEED\n"
"//  PARAM_MAX_PATH_DEPTH\n"
"//  PARAM_MAX_RR_DEPTH\n"
"//  PARAM_MAX_RR_CAP\n"
"//  PARAM_SAMPLE_PER_PIXEL\n"
"//  PARAM_CAMERA_HAS_DOF\n"
"//  PARAM_CAMERA_LENS_RADIUS\n"
"//  PARAM_CAMERA_FOCAL_DISTANCE\n"
"//  PARAM_LOWLATENCY\n"
"//  PARAM_DIRECT_LIGHT_SAMPLING\n"
"\n"
"// (optional)\n"
"//  PARAM_HAVE_INFINITELIGHT\n"
"//  PARAM_IL_GAIN_R\n"
"//  PARAM_IL_GAIN_G\n"
"//  PARAM_IL_GAIN_B\n"
"//  PARAM_IL_SHIFT_U\n"
"//  PARAM_IL_SHIFT_V\n"
"//  PARAM_IL_WIDTH\n"
"//  PARAM_IL_HEIGHT\n"
"\n"
"#ifndef M_PI\n"
"#define M_PI 3.14159265358979323846f\n"
"#endif\n"
"\n"
"#ifndef INV_PI\n"
"#define INV_PI  0.31830988618379067154f\n"
"#endif\n"
"\n"
"#ifndef INV_TWOPI\n"
"#define INV_TWOPI  0.15915494309189533577f\n"
"#endif\n"
"\n"
"#ifndef TRUE\n"
"#define TRUE 1\n"
"#endif\n"
"\n"
"#ifndef FALSE\n"
"#define FALSE 0\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Types\n"
"//------------------------------------------------------------------------------\n"
"\n"
"typedef struct {\n"
"	float r, g, b;\n"
"} Spectrum;\n"
"\n"
"typedef struct {\n"
"	float x, y, z;\n"
"} Point;\n"
"\n"
"typedef struct {\n"
"	float x, y, z;\n"
"} Vector;\n"
"\n"
"typedef struct {\n"
"	unsigned int v0, v1, v2;\n"
"} Triangle;\n"
"\n"
"typedef struct {\n"
"	Point o;\n"
"	Vector d;\n"
"	float mint, maxt;\n"
"} Ray;\n"
"\n"
"typedef struct {\n"
"	float t;\n"
"	float b1, b2; // Barycentric coordinates of the hit point\n"
"	uint index;\n"
"} RayHit;\n"
"\n"
"typedef struct {\n"
"	unsigned int s1, s2, s3;\n"
"} Seed;\n"
"\n"
"typedef struct {\n"
"	Spectrum throughput;\n"
"	unsigned int depth, pixelIndex\n"
"#if !defined (PARAM_LOWLATENCY)\n"
"	, subpixelIndex\n"
"#endif\n"
"	;\n"
"	Seed seed;\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"	int specularBounce;\n"
"#endif\n"
"} Path;\n"
"\n"
"typedef struct {\n"
"	Spectrum c;\n"
"	unsigned int count;\n"
"} Pixel;\n"
"\n"
"#define MAT_MATTE 0\n"
"#define MAT_AREALIGHT 1\n"
"#define MAT_MIRROR 2\n"
"\n"
"typedef struct {\n"
"	unsigned int type;\n"
"	union {\n"
"		struct {\n"
"			float r, g, b;\n"
"		} matte;\n"
"		struct {\n"
"			float gain_r, gain_g, gain_b;\n"
"		} areaLight;\n"
"		struct {\n"
"			float r, g, b;\n"
"			int specularBounce;\n"
"		} mirror;\n"
"	} mat;\n"
"} Material;\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Random number generator\n"
"// maximally equidistributed combined Tausworthe generator\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#define FLOATMASK 0x00ffffffu\n"
"\n"
"unsigned int TAUSWORTHE(const unsigned int s, const unsigned int a,\n"
"	const unsigned int b, const unsigned int c,\n"
"	const unsigned int d) {\n"
"	return ((s&c)<<d) ^ (((s << a) ^ s) >> b);\n"
"}\n"
"\n"
"unsigned int LCG(const unsigned int x) { return x * 69069; }\n"
"\n"
"unsigned int ValidSeed(const unsigned int x, const unsigned int m) {\n"
"	return (x < m) ? (x + m) : x;\n"
"}\n"
"\n"
"void InitRandomGenerator(unsigned int seed, Seed *s) {\n"
"	// Avoid 0 value\n"
"	seed = (seed == 0) ? (seed + 0xffffffu) : seed;\n"
"\n"
"	s->s1 = ValidSeed(LCG(seed), 1);\n"
"	s->s2 = ValidSeed(LCG(s->s1), 7);\n"
"	s->s3 = ValidSeed(LCG(s->s2), 15);\n"
"}\n"
"\n"
"unsigned long RndUintValue(Seed *s) {\n"
"	s->s1 = TAUSWORTHE(s->s1, 13, 19, 4294967294UL, 12);\n"
"	s->s2 = TAUSWORTHE(s->s2, 2, 25, 4294967288UL, 4);\n"
"	s->s3 = TAUSWORTHE(s->s3, 3, 11, 4294967280UL, 17);\n"
"\n"
"	return ((s->s1) ^ (s->s2) ^ (s->s3));\n"
"}\n"
"\n"
"float RndFloatValue(Seed *s) {\n"
"	return (RndUintValue(s) & FLOATMASK) * (1.f / (FLOATMASK + 1UL));\n"
"}\n"
"\n"
"//------------------------------------------------------------------------------\n"
"\n"
"float Dot(const Vector *v0, const Vector *v1) {\n"
"	return v0->x * v1->x + v0->y * v1->y + v0->z * v1->z;\n"
"}\n"
"\n"
"void Normalize(Vector *v) {\n"
"	const float il = 1.f / sqrt(Dot(v, v));\n"
"\n"
"	v->x *= il;\n"
"	v->y *= il;\n"
"	v->z *= il;\n"
"}\n"
"\n"
"void Cross(Vector *v3, const Vector *v1, const Vector *v2) {\n"
"	v3->x = (v1->y * v2->z) - (v1->z * v2->y);\n"
"	v3->y = (v1->z * v2->x) - (v1->x * v2->z),\n"
"	v3->z = (v1->x * v2->y) - (v1->y * v2->x);\n"
"}\n"
"\n"
"void ConcentricSampleDisk(const float u1, const float u2, float *dx, float *dy) {\n"
"	float r, theta;\n"
"	// Map uniform random numbers to $[-1,1]^2$\n"
"	float sx = 2.f * u1 - 1.f;\n"
"	float sy = 2.f * u2 - 1.f;\n"
"	// Map square to $(r,\theta)$\n"
"	// Handle degeneracy at the origin\n"
"	if (sx == 0.f && sy == 0.f) {\n"
"		*dx = 0.f;\n"
"		*dy = 0.f;\n"
"		return;\n"
"	}\n"
"	if (sx >= -sy) {\n"
"		if (sx > sy) {\n"
"			// Handle first region of disk\n"
"			r = sx;\n"
"			if (sy > 0.f)\n"
"				theta = sy / r;\n"
"			else\n"
"				theta = 8.0f + sy / r;\n"
"		} else {\n"
"			// Handle second region of disk\n"
"			r = sy;\n"
"			theta = 2.0f - sx / r;\n"
"		}\n"
"	} else {\n"
"		if (sx <= sy) {\n"
"			// Handle third region of disk\n"
"			r = -sx;\n"
"			theta = 4.0f - sy / r;\n"
"		} else {\n"
"			// Handle fourth region of disk\n"
"			r = -sy;\n"
"			theta = 6.0f + sx / r;\n"
"		}\n"
"	}\n"
"	theta *= M_PI / 4.f;\n"
"	*dx = r * cos(theta);\n"
"	*dy = r * sin(theta);\n"
"}\n"
"\n"
"void CosineSampleHemisphere(Vector *ret, const float u1, const float u2) {\n"
"	ConcentricSampleDisk(u1, u2, &ret->x, &ret->y);\n"
"	ret->z = sqrt(max(0.f, 1.f - ret->x * ret->x - ret->y * ret->y));\n"
"}\n"
"\n"
"void CoordinateSystem(const Vector *v1, Vector *v2, Vector *v3) {\n"
"	if (fabs(v1->x) > fabs(v1->y)) {\n"
"		float invLen = 1.f / sqrt(v1->x * v1->x + v1->z * v1->z);\n"
"		v2->x = -v1->z * invLen;\n"
"		v2->y = 0.f;\n"
"		v2->z = v1->x * invLen;\n"
"	} else {\n"
"		float invLen = 1.f / sqrt(v1->y * v1->y + v1->z * v1->z);\n"
"		v2->x = 0.f;\n"
"		v2->y = v1->z * invLen;\n"
"		v2->z = -v1->y * invLen;\n"
"	}\n"
"\n"
"	Cross(v3, v1, v2);\n"
"}\n"
"\n"
"//------------------------------------------------------------------------------\n"
"\n"
"void GenerateRay(\n"
"		const unsigned int pixelIndex,\n"
"		__global Ray *ray, Seed *seed\n"
"#if defined (PARAM_LOWLATENCY)\n"
"		, __global float *cameraData\n"
"#endif\n"
"		) {\n"
"\n"
"	/*// Gaussina distribution\n"
"	const float rad = filterRadius * sqrt(-log(1.f - RndFloatValue(seed)));\n"
"	const float angle = 2 * M_PI * RndFloatValue(seed);\n"
"\n"
"	const float screenX = pixelIndex % PARAM_IMAGE_WIDTH + rad * cos(angle);\n"
"	const float screenY = pixelIndex / PARAM_IMAGE_WIDTH + rad * sin(angle);*/\n"
"\n"
"	const float screenX = pixelIndex % PARAM_IMAGE_WIDTH + RndFloatValue(seed) - 0.5f;\n"
"	const float screenY = pixelIndex / PARAM_IMAGE_WIDTH + RndFloatValue(seed) - 0.5f;\n"
"\n"
"	Point Pras;\n"
"	Pras.x = screenX;\n"
"	Pras.y = PARAM_IMAGE_HEIGHT - screenY - 1.f;\n"
"	Pras.z = 0;\n"
"\n"
"	Point orig;\n"
"	// RasterToCamera(Pras, &orig);\n"
"#if defined (PARAM_LOWLATENCY)\n"
"	const float iw = 1.f / (cameraData[12] * Pras.x + cameraData[13] * Pras.y + cameraData[14] * Pras.z + cameraData[15]);\n"
"	orig.x = (cameraData[0] * Pras.x + cameraData[1] * Pras.y + cameraData[2] * Pras.z + cameraData[3]) * iw;\n"
"	orig.y = (cameraData[4] * Pras.x + cameraData[5] * Pras.y + cameraData[6] * Pras.z + cameraData[7]) * iw;\n"
"	orig.z = (cameraData[8] * Pras.x + cameraData[9] * Pras.y + cameraData[10] * Pras.z + cameraData[11]) * iw;\n"
"#else\n"
"	const float iw = 1.f / (PARAM_RASTER2CAMERA_30 * Pras.x + PARAM_RASTER2CAMERA_31 * Pras.y + PARAM_RASTER2CAMERA_32 * Pras.z + PARAM_RASTER2CAMERA_33);\n"
"	orig.x = (PARAM_RASTER2CAMERA_00 * Pras.x + PARAM_RASTER2CAMERA_01 * Pras.y + PARAM_RASTER2CAMERA_02 * Pras.z + PARAM_RASTER2CAMERA_03) * iw;\n"
"	orig.y = (PARAM_RASTER2CAMERA_10 * Pras.x + PARAM_RASTER2CAMERA_11 * Pras.y + PARAM_RASTER2CAMERA_12 * Pras.z + PARAM_RASTER2CAMERA_13) * iw;\n"
"	orig.z = (PARAM_RASTER2CAMERA_20 * Pras.x + PARAM_RASTER2CAMERA_21 * Pras.y + PARAM_RASTER2CAMERA_22 * Pras.z + PARAM_RASTER2CAMERA_23) * iw;\n"
"#endif\n"
"\n"
"	Vector dir;\n"
"	dir.x = orig.x;\n"
"	dir.y = orig.y;\n"
"	dir.z = orig.z;\n"
"\n"
"#if defined(PARAM_CAMERA_HAS_DOF)\n"
"	// Sample point on lens\n"
"	float lensU, lensV;\n"
"	ConcentricSampleDisk(RndFloatValue(seed), RndFloatValue(seed), &lensU, &lensV);\n"
"	lensU *= PARAM_CAMERA_LENS_RADIUS;\n"
"	lensV *= PARAM_CAMERA_LENS_RADIUS;\n"
"\n"
"	// Compute point on plane of focus\n"
"	const float ft = (PARAM_CAMERA_FOCAL_DISTANCE - PARAM_CLIP_HITHER) / dir.z;\n"
"	Point Pfocus;\n"
"	Pfocus.x = orig.x + dir.x * ft;\n"
"	Pfocus.y = orig.y + dir.y * ft;\n"
"	Pfocus.z = orig.z + dir.z * ft;\n"
"\n"
"	// Update ray for effect of lens\n"
"	orig.x += lensU * ((PARAM_CAMERA_FOCAL_DISTANCE - PARAM_CLIP_HITHER) / PARAM_CAMERA_FOCAL_DISTANCE);\n"
"	orig.y += lensV * ((PARAM_CAMERA_FOCAL_DISTANCE - PARAM_CLIP_HITHER) / PARAM_CAMERA_FOCAL_DISTANCE);\n"
"\n"
"	dir.x = Pfocus.x - orig.x;\n"
"	dir.y = Pfocus.y - orig.y;\n"
"	dir.z = Pfocus.z - orig.z;\n"
"#endif\n"
"\n"
"	Normalize(&dir);\n"
"\n"
"	// CameraToWorld(*ray, ray);\n"
"	Point torig;\n"
"#if defined (PARAM_LOWLATENCY)\n"
"	const float iw2 = 1.f / (cameraData[16 + 12] * orig.x + cameraData[16 + 13] * orig.y + cameraData[16 + 14] * orig.z + cameraData[16 + 15]);\n"
"	torig.x = (cameraData[16 + 0] * orig.x + cameraData[16 + 1] * orig.y + cameraData[16 + 2] * orig.z + cameraData[16 + 3]) * iw2;\n"
"	torig.y = (cameraData[16 + 4] * orig.x + cameraData[16 + 5] * orig.y + cameraData[16 + 6] * orig.z + cameraData[16 + 7]) * iw2;\n"
"	torig.z = (cameraData[16 + 8] * orig.x + cameraData[16 + 9] * orig.y + cameraData[16 + 12] * orig.z + cameraData[16 + 11]) * iw2;\n"
"#else\n"
"	const float iw2 = 1.f / (PARAM_CAMERA2WORLD_30 * orig.x + PARAM_CAMERA2WORLD_31 * orig.y + PARAM_CAMERA2WORLD_32 * orig.z + PARAM_CAMERA2WORLD_33);\n"
"	torig.x = (PARAM_CAMERA2WORLD_00 * orig.x + PARAM_CAMERA2WORLD_01 * orig.y + PARAM_CAMERA2WORLD_02 * orig.z + PARAM_CAMERA2WORLD_03) * iw2;\n"
"	torig.y = (PARAM_CAMERA2WORLD_10 * orig.x + PARAM_CAMERA2WORLD_11 * orig.y + PARAM_CAMERA2WORLD_12 * orig.z + PARAM_CAMERA2WORLD_13) * iw2;\n"
"	torig.z = (PARAM_CAMERA2WORLD_20 * orig.x + PARAM_CAMERA2WORLD_21 * orig.y + PARAM_CAMERA2WORLD_22 * orig.z + PARAM_CAMERA2WORLD_23) * iw2;\n"
"#endif\n"
"\n"
"	Vector tdir;\n"
"#if defined (PARAM_LOWLATENCY)\n"
"	tdir.x = cameraData[16 + 0] * dir.x + cameraData[16 + 1] * dir.y + cameraData[16 + 2] * dir.z;\n"
"	tdir.y = cameraData[16 + 4] * dir.x + cameraData[16 + 5] * dir.y + cameraData[16 + 6] * dir.z;\n"
"	tdir.z = cameraData[16 + 8] * dir.x + cameraData[16 + 9] * dir.y + cameraData[16 + 10] * dir.z;\n"
"#else\n"
"	tdir.x = PARAM_CAMERA2WORLD_00 * dir.x + PARAM_CAMERA2WORLD_01 * dir.y + PARAM_CAMERA2WORLD_02 * dir.z;\n"
"	tdir.y = PARAM_CAMERA2WORLD_10 * dir.x + PARAM_CAMERA2WORLD_11 * dir.y + PARAM_CAMERA2WORLD_12 * dir.z;\n"
"	tdir.z = PARAM_CAMERA2WORLD_20 * dir.x + PARAM_CAMERA2WORLD_21 * dir.y + PARAM_CAMERA2WORLD_22 * dir.z;\n"
"#endif\n"
"\n"
"	ray->o = torig;\n"
"	ray->d = tdir;\n"
"	ray->mint = PARAM_RAY_EPSILON;\n"
"	ray->maxt = (PARAM_CLIP_YON - PARAM_CLIP_HITHER) / dir.z;\n"
"}\n"
"\n"
"//------------------------------------------------------------------------------\n"
"\n"
"__kernel void Init(\n"
"		__global Path *paths,\n"
"		__global Ray *rays\n"
"#if defined (PARAM_LOWLATENCY)\n"
"		, __global float *cameraData\n"
"#endif\n"
"		) {\n"
"	const int gid = get_global_id(0);\n"
"	if (gid >= PARAM_PATH_COUNT)\n"
"		return;\n"
"\n"
"	// Initialize the path\n"
"	__global Path *path = &paths[gid];\n"
"	path->throughput.r = 1.f;\n"
"	path->throughput.g = 1.f;\n"
"	path->throughput.b = 1.f;\n"
"	path->depth = 0;\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"	path->specularBounce = TRUE;\n"
"#endif\n"
"\n"
"	const unsigned int pixelIndex = (PARAM_STARTLINE * PARAM_IMAGE_WIDTH + gid) % (PARAM_IMAGE_WIDTH * PARAM_IMAGE_HEIGHT);\n"
"	path->pixelIndex = pixelIndex;\n"
"#if !defined (PARAM_LOWLATENCY)\n"
"	path->subpixelIndex = 0;\n"
"#endif\n"
"\n"
"	// Initialize random number generator\n"
"	Seed seed;\n"
"	InitRandomGenerator(PARAM_SEED + gid, &seed);\n"
"\n"
"	// Generate the eye ray\n"
"	GenerateRay(pixelIndex, &rays[gid], &seed\n"
"#if defined (PARAM_LOWLATENCY)\n"
"		, cameraData\n"
"#endif\n"
"		);\n"
"\n"
"	// Save the seed\n"
"	path->seed.s1 = seed.s1;\n"
"	path->seed.s2 = seed.s2;\n"
"	path->seed.s3 = seed.s3;\n"
"}\n"
"\n"
"//------------------------------------------------------------------------------\n"
"\n"
"__kernel void InitFrameBuffer(\n"
"		__global Pixel *frameBuffer) {\n"
"	const int gid = get_global_id(0);\n"
"	if (gid >= PARAM_IMAGE_WIDTH * PARAM_IMAGE_HEIGHT)\n"
"		return;\n"
"\n"
"	__global Pixel *p = &frameBuffer[gid];\n"
"	p->c.r = 0.f;\n"
"	p->c.g = 0.f;\n"
"	p->c.b = 0.f;\n"
"	p->count = 0;\n"
"}\n"
"\n"
"unsigned int Radiance2PixelUInt(const float x) {\n"
"	return (unsigned int)(pow(clamp(x, 0.f, 1.f), 1.f / 2.2f) * 255.f + .5f);\n"
"}\n"
"\n"
"__kernel void UpdatePixelBuffer(\n"
"		__global Pixel *frameBuffer,\n"
"		__global unsigned int *pbo) {\n"
"	const int gid = get_global_id(0);\n"
"	if (gid >= PARAM_IMAGE_WIDTH * PARAM_IMAGE_HEIGHT)\n"
"		return;\n"
"\n"
"	__global Pixel *p = &frameBuffer[gid];\n"
"\n"
"	const unsigned int count = p->count;\n"
"	if (count > 0) {\n"
"		const float invCount = 1.f / p->count;\n"
"\n"
"		const unsigned int r = Radiance2PixelUInt(p->c.r * invCount);\n"
"		const unsigned int g = Radiance2PixelUInt(p->c.g * invCount);\n"
"		const unsigned int b = Radiance2PixelUInt(p->c.b * invCount);\n"
"		pbo[gid] = r | (g << 8) | (b << 16);\n"
"	}\n"
"}\n"
"\n"
"//------------------------------------------------------------------------------\n"
"\n"
"int Mod(int a, int b) {\n"
"	if (b == 0)\n"
"		b = 1;\n"
"\n"
"	a %= b;\n"
"	if (a < 0)\n"
"		a += b;\n"
"\n"
"	return a;\n"
"}\n"
"\n"
"void TexMap_GetTexel(__global Spectrum *pixels, const unsigned int width, const unsigned int height,\n"
"		const int s, const int t, Spectrum *col) {\n"
"	const unsigned int u = Mod(s, width);\n"
"	const unsigned int v = Mod(t, height);\n"
"\n"
"	const unsigned index = v * width + u;\n"
"\n"
"	col->r = pixels[index].r;\n"
"	col->g = pixels[index].g;\n"
"	col->b = pixels[index].b;\n"
"}\n"
"\n"
"void TexMap_GetColor(__global Spectrum *pixels, const unsigned int width, const unsigned int height,\n"
"		const float u, const float v, Spectrum *col) {\n"
"	const float s = u * width - 0.5f;\n"
"	const float t = v * height - 0.5f;\n"
"\n"
"	const int s0 = (int)floor(s);\n"
"	const int t0 = (int)floor(t);\n"
"\n"
"	const float ds = s - s0;\n"
"	const float dt = t - t0;\n"
"\n"
"	const float ids = 1.f - ds;\n"
"	const float idt = 1.f - dt;\n"
"\n"
"	Spectrum c0, c1, c2, c3;\n"
"	TexMap_GetTexel(pixels, width, height, s0, t0, &c0);\n"
"	TexMap_GetTexel(pixels, width, height, s0, t0 + 1, &c1);\n"
"	TexMap_GetTexel(pixels, width, height, s0 + 1, t0, &c2);\n"
"	TexMap_GetTexel(pixels, width, height, s0 + 1, t0 + 1, &c3);\n"
"\n"
"	const float k0 = ids * idt;\n"
"	const float k1 = ids * dt;\n"
"	const float k2 = ds * idt;\n"
"	const float k3 = ds * dt;\n"
"\n"
"	col->r = k0 * c0.r + k1 * c1.r + k2 * c2.r + k3 * c3.r;\n"
"	col->g = k0 * c0.g + k1 * c1.g + k2 * c2.g + k3 * c3.g;\n"
"	col->b = k0 * c0.b + k1 * c1.b + k2 * c2.b + k3 * c3.b;\n"
"}\n"
"\n"
"float SphericalTheta(const Vector *v) {\n"
"	return acos(clamp(v->z, -1.f, 1.f));\n"
"}\n"
"\n"
"float SphericalPhi(const Vector *v) {\n"
"	float p = atan2(v->y, v->x);\n"
"	return (p < 0.f) ? p + 2.f * M_PI : p;\n"
"}\n"
"\n"
"#if defined(PARAM_HAVE_INFINITELIGHT)\n"
"void InfiniteLight_Le(__global Spectrum *infiniteLightMap, Spectrum *le, Vector *dir) {\n"
"	const float u = SphericalPhi(dir) * INV_TWOPI +  PARAM_IL_SHIFT_U;\n"
"	const float v = SphericalTheta(dir) * INV_PI + PARAM_IL_SHIFT_V;\n"
"\n"
"	TexMap_GetColor(infiniteLightMap, PARAM_IL_WIDTH, PARAM_IL_HEIGHT, u, v, le);\n"
"\n"
"	le->r *= PARAM_IL_GAIN_R;\n"
"	le->g *= PARAM_IL_GAIN_G;\n"
"	le->b *= PARAM_IL_GAIN_B;\n"
"}\n"
"#endif\n"
"\n"
"void Mesh_InterpolateColor(__global Spectrum *colors, __global Triangle *triangles,\n"
"		const unsigned int triIndex, const float b1, const float b2, Spectrum *C) {\n"
"	__global Triangle *tri = &triangles[triIndex];\n"
"\n"
"	const float b0 = 1.f - b1 - b2;\n"
"	C->r = b0 * colors[tri->v0].r + b1 * colors[tri->v1].r + b2 * colors[tri->v2].r;\n"
"	C->g = b0 * colors[tri->v0].g + b1 * colors[tri->v1].g + b2 * colors[tri->v2].g;\n"
"	C->b = b0 * colors[tri->v0].b + b1 * colors[tri->v1].b + b2 * colors[tri->v2].b;\n"
"}\n"
"\n"
"void Mesh_InterpolateNormal(__global Vector *normals, __global Triangle *triangles,\n"
"		const unsigned int triIndex, const float b1, const float b2, Vector *N) {\n"
"	__global Triangle *tri = &triangles[triIndex];\n"
"\n"
"	const float b0 = 1.f - b1 - b2;\n"
"	N->x = b0 * normals[tri->v0].x + b1 * normals[tri->v1].x + b2 * normals[tri->v2].x;\n"
"	N->y = b0 * normals[tri->v0].y + b1 * normals[tri->v1].y + b2 * normals[tri->v2].y;\n"
"	N->z = b0 * normals[tri->v0].z + b1 * normals[tri->v1].z + b2 * normals[tri->v2].z;\n"
"\n"
"	Normalize(N);\n"
"}\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Materials\n"
"//------------------------------------------------------------------------------\n"
"\n"
"void Matte_Sample_f(__global Material *mat, const Vector *wo, Vector *wi,\n"
"		float *pdf, Spectrum *f, const Vector *shadeN,\n"
"		const float u0, const float u1,  const float u2\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"		, __global int *specularBounce\n"
"#endif\n"
"		) {\n"
"	Vector dir;\n"
"	CosineSampleHemisphere(&dir, u0, u1);\n"
"	*pdf = dir.z * INV_PI;\n"
"\n"
"	Vector v1, v2;\n"
"	CoordinateSystem(shadeN, &v1, &v2);\n"
"\n"
"	wi->x = v1.x * dir.x + v2.x * dir.y + shadeN->x * dir.z;\n"
"	wi->y = v1.y * dir.x + v2.y * dir.y + shadeN->y * dir.z;\n"
"	wi->z = v1.z * dir.x + v2.z * dir.y + shadeN->z * dir.z;\n"
"\n"
"	const float dp = Dot(shadeN, wi);\n"
"	// Using 0.0001 instead of 0.0 to cut down fireflies\n"
"	if (dp <= 0.0001f)\n"
"		*pdf = 0.f;\n"
"	else {\n"
"		*pdf /=  dp;\n"
"\n"
"		f->r = mat->mat.matte.r * INV_PI;\n"
"		f->g = mat->mat.matte.g * INV_PI;\n"
"		f->b = mat->mat.matte.b * INV_PI;\n"
"	}\n"
"\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"	*specularBounce = FALSE;\n"
"#endif\n"
"}\n"
"\n"
"void AreaLight_Le(__global Material *mat, const Vector *N, const Vector *wo, Spectrum *Le) {\n"
"	if (Dot(N, wo) <= 0.f) {\n"
"		Le->r = 0.f;\n"
"		Le->g = 0.f;\n"
"		Le->b = 0.f;\n"
"	} else {\n"
"		Le->r = mat->mat.areaLight.gain_r;\n"
"		Le->g = mat->mat.areaLight.gain_g;\n"
"		Le->b = mat->mat.areaLight.gain_b;\n"
"	}\n"
"}\n"
"\n"
"void Mirror_Sample_f(__global Material *mat, const Vector *rayDir, Vector *wi,\n"
"		float *pdf, Spectrum *f, const Vector *shadeN, const float RdotShadeN\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"		, __global int *specularBounce\n"
"#endif\n"
"		) {\n"
"	wi->x = rayDir->x + (2.f * RdotShadeN) * shadeN->x;\n"
"	wi->y = rayDir->y + (2.f * RdotShadeN) * shadeN->y;\n"
"	wi->z = rayDir->z + (2.f * RdotShadeN) * shadeN->z;\n"
"\n"
"	*pdf = 1.f;\n"
"\n"
"	f->r = mat->mat.mirror.r;\n"
"	f->g = mat->mat.mirror.g;\n"
"	f->b = mat->mat.mirror.b;\n"
"\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"	*specularBounce = mat->mat.mirror.specularBounce;\n"
"#endif\n"
"}\n"
"\n"
"//------------------------------------------------------------------------------\n"
"\n"
"void TerminatePath(__global Path *path, __global Ray *ray, __global Pixel *frameBuffer, Seed *seed, Spectrum *radiance\n"
"#if defined (PARAM_LOWLATENCY)\n"
"		, __global float *cameraData\n"
"#endif\n"
"		) {\n"
"	// Add sample to the framebuffer\n"
"\n"
"	const unsigned int pixelIndex = path->pixelIndex;\n"
"	__global Pixel *pixel = &frameBuffer[pixelIndex];\n"
"\n"
"	pixel->c.r += isnan(radiance->r) ? 0.f : radiance->r;\n"
"	pixel->c.g += isnan(radiance->g) ? 0.f : radiance->g;\n"
"	pixel->c.b += isnan(radiance->b) ? 0.f : radiance->b;\n"
"	pixel->count += 1;\n"
"\n"
"	unsigned int newPixelIndex;\n"
"#if defined (PARAM_LOWLATENCY)\n"
"	newPixelIndex = (pixelIndex + PARAM_PATH_COUNT) % (PARAM_IMAGE_WIDTH * PARAM_IMAGE_HEIGHT);\n"
"	path->pixelIndex = newPixelIndex;\n"
"#else\n"
"	const unsigned int subpixelIndex = path->subpixelIndex;\n"
"	if (subpixelIndex >= PARAM_SAMPLE_PER_PIXEL) {\n"
"		newPixelIndex = (pixelIndex + PARAM_PATH_COUNT) % (PARAM_IMAGE_WIDTH * PARAM_IMAGE_HEIGHT);\n"
"		path->pixelIndex = newPixelIndex;\n"
"		path->subpixelIndex = 0;\n"
"	} else {\n"
"		newPixelIndex = pixelIndex;\n"
"		path->subpixelIndex = subpixelIndex + 1;\n"
"	}\n"
"#endif\n"
"\n"
"	GenerateRay(newPixelIndex, ray, seed\n"
"#if defined (PARAM_LOWLATENCY)\n"
"		, cameraData\n"
"#endif\n"
"		);\n"
"\n"
"	// Re-initialize the path\n"
"	path->throughput.r = 1.f;\n"
"	path->throughput.g = 1.f;\n"
"	path->throughput.b = 1.f;\n"
"	path->depth = 0;\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"	path->specularBounce = TRUE;\n"
"#endif\n"
"}\n"
"\n"
"__kernel void AdvancePaths(\n"
"		__global Path *paths,\n"
"		__global Ray *rays,\n"
"		__global RayHit *rayHits,\n"
"		__global Pixel *frameBuffer,\n"
"		__global Material *mats,\n"
"		__global unsigned int *meshMats,\n"
"		__global unsigned int *meshIDs, // Not used\n"
"		__global unsigned int *triIDs,\n"
"		__global Spectrum *vertColors,\n"
"		__global Vector *vertNormals,\n"
"		__global Triangle *triangles\n"
"#if defined (PARAM_LOWLATENCY)\n"
"		, __global float *cameraData\n"
"#endif\n"
"#if defined(PARAM_HAVE_INFINITELIGHT)\n"
"		, __global Spectrum *infiniteLightMap\n"
"#endif\n"
"		) {\n"
"	const int gid = get_global_id(0);\n"
"	if (gid >= PARAM_PATH_COUNT)\n"
"		return;\n"
"\n"
"	__global Path *path = &paths[gid];\n"
"\n"
"	// Read the seed\n"
"	Seed seed;\n"
"	seed.s1 = path->seed.s1;\n"
"	seed.s2 = path->seed.s2;\n"
"	seed.s3 = path->seed.s3;\n"
"\n"
"	__global Ray *ray = &rays[gid];\n"
"	Vector rayDir;\n"
"	rayDir.x = ray->d.x;\n"
"	rayDir.y = ray->d.y;\n"
"	rayDir.z = ray->d.z;\n"
"\n"
"	Spectrum throughput;\n"
"	throughput.r = path->throughput.r;\n"
"	throughput.g = path->throughput.g;\n"
"	throughput.b = path->throughput.b;\n"
"\n"
"	__global RayHit *rayHit = &rayHits[gid];\n"
"	const unsigned int currentTriangleIndex = rayHit->index;\n"
"	if (currentTriangleIndex != 0xffffffffu ) {\n"
"		// Something was hit\n"
"\n"
"		// Interpolate Color\n"
"		Spectrum shadeColor;\n"
"		Mesh_InterpolateColor(vertColors, triangles, currentTriangleIndex, rayHit->b1, rayHit->b2, &shadeColor);\n"
"		throughput.r *= shadeColor.r;\n"
"		throughput.g *= shadeColor.g;\n"
"		throughput.b *= shadeColor.b;\n"
"\n"
"		// Interpolate the normal\n"
"		Vector N;\n"
"		Mesh_InterpolateNormal(vertNormals, triangles, currentTriangleIndex, rayHit->b1, rayHit->b2, &N);\n"
"\n"
"		// Flip the normal if required\n"
"		Vector shadeN;\n"
"		float RdotShadeN = Dot(&rayDir, &N);\n"
"\n"
"		const float nFlip = (RdotShadeN > 0.f) ? -1.f : 1.f;\n"
"		shadeN.x = nFlip * N.x;\n"
"		shadeN.y = nFlip * N.y;\n"
"		shadeN.z = nFlip * N.z;\n"
"		RdotShadeN = -nFlip * RdotShadeN;\n"
"\n"
"		const unsigned int meshIndex = meshIDs[currentTriangleIndex];\n"
"		__global Material *mat = &mats[meshMats[meshIndex]];\n"
"\n"
"		const float u0 = RndFloatValue(&seed);\n"
"		const float u1 = RndFloatValue(&seed);\n"
"		const float u2 = RndFloatValue(&seed);\n"
"\n"
"		Vector wo;\n"
"		wo.x = -rayDir.x;\n"
"		wo.y = -rayDir.y;\n"
"		wo.z = -rayDir.z;\n"
"\n"
"		Vector wi;\n"
"		float pdf;\n"
"		Spectrum f;\n"
"\n"
"		Spectrum materialLe;\n"
"		materialLe.r = 0.f;\n"
"		materialLe.g = 0.f;\n"
"		materialLe.b = 0.f;\n"
"		bool areaLightHit = false;\n"
"		switch (mat->type) {\n"
"			case MAT_MATTE:\n"
"				Matte_Sample_f(mat, &wo, &wi, &pdf, &f, &shadeN, u0, u1, u2\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"					, &path->specularBounce\n"
"#endif\n"
"					);\n"
"				break;\n"
"			case MAT_AREALIGHT:\n"
"				areaLightHit = true;\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"				if (path->specularBounce)\n"
"#endif\n"
"					AreaLight_Le(mat, &N, &wo, &materialLe);\n"
"				break;\n"
"			case MAT_MIRROR:\n"
"				Mirror_Sample_f(mat, &rayDir, &wi, &pdf, &f, &shadeN, RdotShadeN\n"
"#if defined(PARAM_DIRECT_LIGHT_SAMPLING)\n"
"					, &path->specularBounce\n"
"#endif\n"
"					);\n"
"				break;\n"
"			default:\n"
"				// Huston, we have a problem...\n"
"				pdf = 0.f;\n"
"				break;\n"
"		}\n"
"\n"
"		// Russian roulette\n"
"		const float rrProb = max(max(throughput.r, max(throughput.g, throughput.b)), (float)PARAM_RR_CAP);\n"
"\n"
"		const unsigned int pathDepth = path->depth + 1;\n"
"		const float rrSample = RndFloatValue(&seed);\n"
"		bool terminatePath = areaLightHit || (pdf <= 0.f) || (pathDepth >= PARAM_MAX_PATH_DEPTH) ||\n"
"			((pathDepth > PARAM_RR_DEPTH) && (rrProb < rrSample));\n"
"\n"
"		const float invRRProb = 1.f / rrProb;\n"
"		throughput.r *= invRRProb;\n"
"		throughput.g *= invRRProb;\n"
"		throughput.b *= invRRProb;\n"
"\n"
"		if (terminatePath) {\n"
"			Spectrum radiance;\n"
"			radiance.r = materialLe.r * path->throughput.r;\n"
"			radiance.g = materialLe.g * path->throughput.g;\n"
"			radiance.b = materialLe.b * path->throughput.b;\n"
"\n"
"			TerminatePath(path, ray, frameBuffer, &seed, &radiance\n"
"#if defined (PARAM_LOWLATENCY)\n"
"				, cameraData\n"
"#endif\n"
"				);\n"
"		} else {\n"
"			const float invPdf = 1.f / pdf;\n"
"			path->throughput.r = throughput.r * f.r * invPdf;\n"
"			path->throughput.g = throughput.g * f.g * invPdf;\n"
"			path->throughput.b = throughput.b * f.b * invPdf;\n"
"\n"
"			// Setup next ray\n"
"			const float t = rayHit->t;\n"
"			ray->o.x = ray->o.x + rayDir.x * t;\n"
"			ray->o.y = ray->o.y + rayDir.y * t;\n"
"			ray->o.z = ray->o.z + rayDir.z * t;\n"
"\n"
"			ray->d.x = wi.x;\n"
"			ray->d.y = wi.y;\n"
"			ray->d.z = wi.z;\n"
"\n"
"			path->depth = pathDepth;\n"
"		}\n"
"	} else {\n"
"		Spectrum radiance;\n"
"\n"
"#if defined(PARAM_HAVE_INFINITELIGHT)\n"
"		Spectrum Le;\n"
"		InfiniteLight_Le(infiniteLightMap, &Le, &rayDir);\n"
"\n"
"		radiance.r = Le.r * path->throughput.r;\n"
"		radiance.g = Le.g * path->throughput.g;\n"
"		radiance.b = Le.b * path->throughput.b;\n"
"#else\n"
"		radiance.r = 0.f;\n"
"		radiance.g = 0.f;\n"
"		radiance.b = 0.f;\n"
"#endif\n"
"\n"
"		TerminatePath(path, ray, frameBuffer, &seed, &radiance\n"
"#if defined (PARAM_LOWLATENCY)\n"
"				, cameraData\n"
"#endif\n"
"			);\n"
"	}\n"
"\n"
"	// Save the seed\n"
"	path->seed.s1 = seed.s1;\n"
"	path->seed.s2 = seed.s2;\n"
"	path->seed.s3 = seed.s3;\n"
"}\n"
;

// 
// HIP OpenGL Interop Example
// 
// Copyright (c) 2025 Jonathan Tainer
//

#include <hip/hip_runtime.h>
#include <hip/hip_gl_interop.h>
#include <raylib.h>
#include <GL/gl.h>
#include <iostream>
#include "stdio.h"

struct HIPSurfaceWrapper {
	hipGraphicsResource* resource;
	hipArray* array;
	hipSurfaceObject_t surfRef;
	hipResourceDesc desc = {};
};

hipStream_t CreateHIPStream() {
	// Get the device of the current OpenGL context
	unsigned int gl_device_count;
	int hip_device;
	hipError_t err = hipGLGetDevices(&gl_device_count, &hip_device, 1, hipGLDeviceList::hipGLDeviceListAll);
	if (err != hipSuccess) {
		std::cerr << "Failed to enumerate HIP devices" << std::endl;
		std::exit(1);
	}
	else if (gl_device_count == 0) {
		std::cerr << "No GL devices found" << std::endl;
		std::exit(1);
	}

	// Make HIP use the same device as OpenGL
	err = hipSetDevice(hip_device);
	if (err != hipSuccess) {
		std::cerr << "Failed to set HIP device" << std::endl;
		std::exit(1);
	}

	// Create a stream for the 
	hipStream_t hip_stream;
	err = hipStreamCreate(&hip_stream);
	if (err != hipSuccess) {
		std::cerr << "Failed to create HIP stream" << std::endl;
		std::exit(1);
	}

	return hip_stream;
}

void DestroyHIPStream(hipStream_t hip_stream) {
	hipError_t err = hipStreamDestroy(hip_stream);
	if (err != hipSuccess) {
		std::cerr << "Failed to destroy HIP stream" << std::endl;
	}

	return;
}

HIPSurfaceWrapper LoadHIPSurfaceFromTexture(Texture texture) {
	HIPSurfaceWrapper surface = {};

	hipError_t err = hipGraphicsGLRegisterImage(&surface.resource, texture.id, GL_TEXTURE_2D, hipGraphicsRegisterFlagsNone);
	if (err != hipSuccess) {
		std::cerr << "Failed to register OpenGL texture" << std::endl;
		std::exit(1);
	}

	err = hipGraphicsMapResources(1, &surface.resource, 0);
	if (err != hipSuccess) {
		std::cerr << "Failed to map OpenGL texture resource" << std::endl;
		std::exit(1);
	}

	err = hipGraphicsSubResourceGetMappedArray(&surface.array, surface.resource, 0, 0);
	if (err != hipSuccess) {
		std::cerr << "Failed to get pointer to mapped resource" << std::endl;
		std::exit(1);
	}

	surface.desc.resType = hipResourceTypeArray;
	surface.desc.res.array.array = surface.array;
	err = hipCreateSurfaceObject(&surface.surfRef, &surface.desc);
	if (err != hipSuccess) {
		std::cerr << "Failed to create HIP surface object" << std::endl;
		std::exit(1);
	}

	return surface;
}

void UnloadHIPSurface(HIPSurfaceWrapper surface) {
	hipError_t err = hipDestroySurfaceObject(surface.surfRef);
	if (err != hipSuccess) {
		std::cerr << "Failed to destroy HIP surface object" << std::endl;
	}

	err = hipGraphicsUnmapResources(1, &surface.resource, 0);
	if (err != hipSuccess) {
		std::cerr << "Failed to unmap texture resource" << std::endl;
	}

	err = hipGraphicsUnregisterResource(surface.resource);
	if (err != hipSuccess) {
		std::cerr << "Failed to unregister texture resource" << std::endl;
	}

	return;
}

__global__ void calculate_step(hipSurfaceObject_t surf) {
	unsigned int x = threadIdx.x;
	unsigned int y = blockIdx.x;

	constexpr int2 neighborOffsets[8] = {
		{-1, -1},
		{0, -1},
		{1, -1},
		{-1, 0},
		{1, 0},
		{-1, 1},
		{0, 1},
		{1, 1}
	};

	// Count number of neighbors who are alive
	int neighborCount = 0;
	for (int2 offset : neighborOffsets) {
		float4 val;
		surf2Dread(&val, surf, (x+offset.x)*sizeof(float4), y+offset.y);
		if (val.x > 0.f) {
			++neighborCount;
		}
	}

	// Check if current cell is alive
	float4 val;
	surf2Dread(&val, surf, x*sizeof(float4), y);
	bool alive = (val.x > 0.f);

	// Game of Life update logic
	if (alive) {
		if (neighborCount < 2 || neighborCount > 3)
			alive = false;
	}
	else if (neighborCount == 3)
		alive = true;

	// Store updated state in the green color channel
	val.y = alive ? 1.f : 0.f;
	surf2Dwrite(val, surf, x*sizeof(float4), y);

	return;
}

__global__ void update_texture(hipSurfaceObject_t surf) {
	unsigned int x = threadIdx.x;
	unsigned int y = blockIdx.x;
	
	// Copy updated state from green color channel to all color channels
	float4 val;
	surf2Dread(&val, surf, x*sizeof(float4), y);
	val.x = val.y;
	val.z = val.y;
	val.w = 1.f;
	surf2Dwrite(val, surf, x*sizeof(float4), y);
	return;
}

__global__ void monolithic_kernel(hipSurfaceObject_t surf) {
	unsigned int x = threadIdx.x;
	unsigned int y = blockIdx.x;
}

int main() {
	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(1024, 1024, "HIP Raylib Interop Example");

	Image image = GenImageColor(1024, 1024, RED);

	// For some reason using uchar4 wasn't working correctly so I am converting the image to float4
	ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);

	// Fill image with noise
	for (int i = 0; i < 1024*1024; ++i) {
		float4* buf = (float4*) image.data;
		float4* ptr = &buf[i];
		float fval = 0.f;
		if (GetRandomValue(0, 3) == 0) {
			ptr->x = 1.f;
			ptr->y = 1.f;
			ptr->z = 1.f;
			ptr->w = 1.f;
		}
		else {
			ptr->x = 0.f;
			ptr->y = 0.f;
			ptr->z = 0.f;
			ptr->w = 1.f;
		}
		
	}

	Texture texture = LoadTextureFromImage(image);
	UnloadImage(image);

	hipStream_t hip_stream = CreateHIPStream();
	HIPSurfaceWrapper surface = LoadHIPSurfaceFromTexture(texture);

	while (!WindowShouldClose()) {
		dim3 block_dim(1024, 1, 1);
		dim3 thread_dim(1024, 1, 1);
		
		calculate_step<<<block_dim, thread_dim, 0, hip_stream>>>(surface.surfRef);
		update_texture<<<block_dim, thread_dim, 0, hip_stream>>>(surface.surfRef);

		hipError_t err = hipStreamSynchronize(hip_stream);
		if (err != hipSuccess) {
			std::cerr << "Failed to sync HIP stream" << std::endl;
		}

		BeginDrawing();
		ClearBackground(BLACK);
		DrawTexture(texture, 0, 0, WHITE);
		DrawFPS(10, 10);
		EndDrawing();
	}

	UnloadHIPSurface(surface);

	DestroyHIPStream(hip_stream);

	UnloadTexture(texture);

	CloseWindow();

	return 0;
}

//======= Copyright (c) Stereolabs Corporation, All rights reserved. ===============

#include "StereolabsPrivatePCH.h"
#include "Stereolabs/Public/Core/StereolabsTexture.h"
#include "Stereolabs/Public/Core/StereolabsCameraProxy.h"
#include "Stereolabs/Public/Utilities/StereolabsMatFunctionLibrary.h"

#include "D3D11RHIBasePrivate.h"
#include "D3D11StateCachePrivate.h"
#include "D3D11State.h"
typedef FD3D11StateCacheBase FD3D11StateCache;
#include "D3D11Resources.h"

#define PLUGIN_OPENGL
#include "OpenGLWindows.h"
#include "BoundShaderStateCache.h"
#include "OpenGLUtil.h"
#include "OpenGLResources.h"

#include "AllowWindowsPlatformTypes.h" 
#include <cuda.h>
#include <cuda_d3d11_interop.h>
#include <cuda_gl_interop.h>
#include "HideWindowsPlatformTypes.h"

DEFINE_LOG_CATEGORY(SlTexture);

#define CHECK_UPDATE_VALID()\
	if (MemoryType == ESlMemoryType::MT_CPU)\
	{\
		SL_LOG_E(SlTexture, "Trying to update texture %s which is a CPU texture.", *Name.ToString());\
		return;\
	}\
	else if (!Texture)\
	{\
		SL_LOG_E(SlTexture, "Trying to update texture %s which is not created.", *Name.ToString());\
		return;\
	}\
	else if (!CudaResource)\
	{\
		SL_LOG_E(SlTexture, "Trying to update texture %s which cuda resource is not registered.", *Name.ToString());\
		return;\
	}\

#define CHECK_CUDA_MEMCPY(CudaError)\
	if (CudaError != cudaSuccess)\
	{\
		SL_LOG_E(SlTexture, "Error while updating texture %s: %s", *Name.ToString(), *FString(cudaGetErrorString(CudaError)));\
	}\

USlTexture::USlTexture()
	:
	Texture(nullptr),
	CudaResource(nullptr)
{
}

void USlTexture::BeginDestroy()
{
	if (CudaResource)
	{
		if (GSlCameraProxy && GSlCameraProxy->IsCameraOpened())
		{
			GSlCameraProxy->PushCudaContext();

			cudaGraphicsUnregisterResource(CudaResource);

			GSlCameraProxy->PopCudaContext();
		}

		CudaResource = nullptr;
	}

	Mat.Mat.free();

	Super::BeginDestroy();
}

void USlTexture::BP_UpdateTexture()
{
#if WITH_EDITOR
	CHECK_UPDATE_VALID();
#endif

	ENQUEUE_RENDER_COMMAND(UpdateGPUTexture)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			GSlCameraProxy->PushCudaContext();

			cudaGraphicsMapResources(1, &CudaResource, 0);

			UpdateTexture();

			cudaGraphicsUnmapResources(1, &CudaResource, 0);

			GSlCameraProxy->PopCudaContext();
		}
	);
}

void USlTexture::BP_UpdateTextureWithMat(const FSlMat& NewMat)
{
#if WITH_EDITOR
	CHECK_UPDATE_VALID();
#endif

	ENQUEUE_RENDER_COMMAND(UpdateGPUTexture)(
		[this, &NewMat = NewMat](FRHICommandListImmediate& RHICmdList)
		{
			GSlCameraProxy->PushCudaContext();

			cudaGraphicsMapResources(1, &CudaResource, 0);

			UpdateTexture(NewMat);

			cudaGraphicsUnmapResources(1, &CudaResource, 0);

			GSlCameraProxy->PopCudaContext();
		}
	);
}

void USlTexture::UpdateTexture()
{
#if WITH_EDITOR
	CHECK_UPDATE_VALID();
#endif

	cudaArray_t TransitionArray = nullptr;
	sl::MEM MemType = sl::MEM::MEM_GPU;

	cudaGraphicsSubResourceGetMappedArray(&TransitionArray, CudaResource, 0, 0);
	cudaError_t CudaError = cudaMemcpy2DToArray(TransitionArray, 0, 0, Mat.Mat.getPtr<sl::uchar1>(MemType), Mat.Mat.getStepBytes(MemType), Mat.Mat.getWidthBytes(), Mat.Mat.getHeight(), cudaMemcpyDeviceToDevice);

#if WITH_EDITOR
	CHECK_CUDA_MEMCPY(CudaError)
#endif
}

void USlTexture::UpdateTexture(const FSlMat& NewMat)
{
#if WITH_EDITOR
	CHECK_UPDATE_VALID();
#endif

	cudaArray_t TransitionArray = nullptr;
	sl::MEM MemType = sl::MEM::MEM_GPU;

	cudaGraphicsSubResourceGetMappedArray(&TransitionArray, CudaResource, 0, 0);
	cudaError_t CudaError = cudaMemcpy2DToArray(TransitionArray, 0, 0, NewMat.Mat.getPtr<sl::uchar1>(MemType), NewMat.Mat.getStepBytes(MemType), NewMat.Mat.getWidthBytes(), NewMat.Mat.getHeight(), cudaMemcpyDeviceToDevice);

#if WITH_EDITOR
	CHECK_CUDA_MEMCPY(CudaError)
#endif
}

void USlTexture::UpdateTexture(const sl::Mat& NewMat)
{
#if WITH_EDITOR
	CHECK_UPDATE_VALID();
#endif

	cudaArray_t TransitionArray = nullptr;
	sl::MEM MemType = sl::MEM::MEM_GPU;

	cudaGraphicsSubResourceGetMappedArray(&TransitionArray, CudaResource, 0, 0);
	cudaError_t CudaError = cudaMemcpy2DToArray(TransitionArray, 0, 0, NewMat.getPtr<sl::uchar1>(MemType), NewMat.getStepBytes(MemType), NewMat.getWidthBytes(), NewMat.getHeight(), cudaMemcpyDeviceToDevice);

#if WITH_EDITOR
	CHECK_CUDA_MEMCPY(CudaError)
#endif
}

bool USlTexture::Resize(int32 NewWidth, int32 NewHeight)
{
#if WITH_EDITOR
	if (!Texture && MemoryType == ESlMemoryType::MT_GPU)
	{
		SL_LOG_E(SlTexture, "Trying to resize unallocated texture.");

		return false;
	}
	else if (NewWidth <= 0 || NewHeight <= 0)
	{
		SL_LOG_E(SlTexture, "Trying to resize texture with size <= 0: %d - %d.", NewWidth, NewHeight);

		return false;
	}
#endif

	Width = NewWidth;
	Height = NewHeight;

	if (MemoryType == ESlMemoryType::MT_GPU && Texture)
	{
		TextureCompressionSettings Compression = Texture->CompressionSettings;

		Texture->ConditionalBeginDestroy();
		Texture = nullptr;

		if (CudaResource)
		{
			if (GSlCameraProxy && GSlCameraProxy->IsCameraOpened())
			{
				cudaGraphicsUnregisterResource(CudaResource);
			}

			CudaResource = nullptr;
		}

		InitResources(TextureFormat, Compression);
	}

	return true;
}

USlViewTexture* USlViewTexture::CreateGPUViewTexture(const FName& TextureName, int32 TextureWidth, int32 TextureHeight, ESlView TextureViewType, bool bCreateTexture2D/* = true*/, ESlTextureFormat TextureFormat/* = ESlTextureFormat::TF_B8G8R8A8_UNORM*/)
{
	return USlViewTexture::CreateViewTexture(TextureName, TextureWidth, TextureHeight, TextureViewType, ESlMemoryType::MT_GPU, bCreateTexture2D, TextureFormat);
}

USlViewTexture* USlViewTexture::CreateCPUViewTexture(const FName& TextureName, int32 TextureWidth, int32 TextureHeight, ESlView TextureViewType)
{
	return USlViewTexture::CreateViewTexture(TextureName, TextureWidth, TextureHeight, TextureViewType, ESlMemoryType::MT_CPU, false, ESlTextureFormat::TF_Unkown);
}

USlViewTexture* USlViewTexture::CreateViewTexture(const FName& TextureName, int32 TextureWidth, int32 TextureHeight, ESlView TextureViewType, ESlMemoryType TextureMemoryType, bool bCreateTexture2D, ESlTextureFormat TextureFormat)
{
#if WITH_EDITOR
	if (TextureWidth <= 0 || TextureHeight <= 0)
	{
		SL_LOG_E(SlTexture, "Trying to create texture with size <= 0: %d - %d.", TextureWidth, TextureHeight);

		return nullptr;
	}
#endif

	USlViewTexture* ViewTexture = NewObject<USlViewTexture>();
	ViewTexture->Width = TextureWidth;
	ViewTexture->Height = TextureHeight;
	ViewTexture->Name = TextureName;
	ViewTexture->ViewType = TextureViewType;
	ViewTexture->MemoryType = TextureMemoryType;
	ViewTexture->TextureType = ESlTextureType::TT_View;
	ViewTexture->TextureFormat = TextureFormat;

	USlMatFunctionLibrary::SetName(ViewTexture->Mat, TextureName);

	if (TextureMemoryType == ESlMemoryType::MT_GPU && bCreateTexture2D)
	{
		ViewTexture->InitResources(TextureFormat, TextureCompressionSettings::TC_VectorDisplacementmap);
	}

	return ViewTexture;
}

USlMeasureTexture* USlMeasureTexture::CreateGPUMeasureTexture(const FName& TextureName, int32 TextureWidth, int32 TextureHeight, ESlMeasure TextureMeasureType, bool bCreateTexture2D/* = true*/, ESlTextureFormat TextureFormat/* = ESlTextureFormat::TF_R32_FLOAT*/)
{
	return USlMeasureTexture::CreateMeasureTexture(TextureName, TextureWidth, TextureHeight, TextureMeasureType, ESlMemoryType::MT_GPU, bCreateTexture2D, TextureFormat);
}

USlMeasureTexture* USlMeasureTexture::CreateCPUMeasureTexture(const FName& TextureName, int32 TextureWidth, int32 TextureHeight, ESlMeasure TextureMeasureType)
{
	return USlMeasureTexture::CreateMeasureTexture(TextureName, TextureWidth, TextureHeight, TextureMeasureType, ESlMemoryType::MT_CPU, false, ESlTextureFormat::TF_Unkown);
}

USlMeasureTexture* USlMeasureTexture::CreateMeasureTexture(const FName& TextureName, int32 TextureWidth, int32 TextureHeight, ESlMeasure TextureMeasureType, ESlMemoryType TextureMemoryType, bool bCreateTexture2D, ESlTextureFormat TextureFormat)
{
	checkf(GSlCameraProxy && GSlCameraProxy->IsCameraOpened(), TEXT("Camera must be opened before creating a GPU texture"));

#if WITH_EDITOR
	if (TextureWidth <= 0 || TextureHeight <= 0)
	{
		SL_LOG_E(SlTexture, "Trying to create texture with size <= 0: %d - %d.", TextureWidth, TextureHeight);

		return nullptr;
	}
#endif

	USlMeasureTexture* MeasureTexture = NewObject<USlMeasureTexture>();
	MeasureTexture->Width = TextureWidth;
	MeasureTexture->Height = TextureHeight;
	MeasureTexture->Name = TextureName;
	MeasureTexture->MeasureType = TextureMeasureType;
	MeasureTexture->MemoryType = TextureMemoryType;
	MeasureTexture->TextureType = ESlTextureType::TT_Measure;
	MeasureTexture->TextureFormat = TextureFormat;

	USlMatFunctionLibrary::SetName(MeasureTexture->Mat, TextureName);

	if (TextureMemoryType == ESlMemoryType::MT_GPU && bCreateTexture2D)
	{
		MeasureTexture->InitResources(TextureFormat, TextureCompressionSettings::TC_DistanceFieldFont);
	}

	return MeasureTexture;
}

void USlTexture::InitResources(ESlTextureFormat Format, TextureCompressionSettings Compression)
{
	// Create texture
	Texture = UTexture2D::CreateTransient(Width, Height, GetPixelFormatFromSlTextureFormat(Format));
#if WITH_EDITORONLY_DATA
	Texture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
	Texture->CompressionNone = true;
	Texture->CompressionNoAlpha = true;
	Texture->DeferCompression = false;
#endif
	Texture->bNoTiling = true;
	Texture->bIsStreamable = false;
	Texture->SRGB = false;
	Texture->CompressionSettings = Compression;
	Texture->Filter = TextureFilter::TF_Bilinear;
	Texture->AddressX = TextureAddress::TA_Clamp;
	Texture->AddressY = TextureAddress::TA_Clamp;
	Texture->LODGroup = TextureGroup::TEXTUREGROUP_RenderTarget;

	Texture->UpdateResource();

	FlushRenderingCommands();

	GSlCameraProxy->PushCudaContext();

	cudaError_t CudaError = cudaError::cudaSuccess;
	FString RHIName = GDynamicRHI->GetName();

	if (RHIName.Equals("D3D11"))
	{
		FD3D11TextureBase* D3D11Texture = GetD3D11TextureFromRHITexture(Texture->Resource->TextureRHI);
		CudaError = cudaGraphicsD3D11RegisterResource(&CudaResource, D3D11Texture->GetResource(), cudaGraphicsMapFlagsNone);
	}
	else if (RHIName.Equals("OpenGL"))
	{
		FOpenGLTextureBase* OpenGLTexture = GetOpenGLTextureFromRHITexture(Texture->Resource->TextureRHI);
		CudaError = cudaGraphicsGLRegisterImage(&CudaResource, OpenGLTexture->Resource, OpenGLTexture->Target, cudaGraphicsMapFlagsNone);
	}
	else
	{
		SL_LOG_F(SlTexture, "Selected RHI not supported : %s", *RHIName);
		CudaError = cudaError::cudaErrorInvalidTexture;
	}

#if WITH_EDITOR
	if (CudaError != cudaError::cudaSuccess)
	{
		SL_LOG_E(SlTexture, "Error while registering resource %s: %s", *Name.ToString(), *FString(cudaGetErrorString(CudaError)));
	}
#endif

	GSlCameraProxy->PopCudaContext();
}
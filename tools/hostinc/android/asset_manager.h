// Stub minimal untuk uji host (tools/game_sim.cpp). Bukan header Android asli.
#pragma once
#include <stdint.h>
#include <sys/types.h>
struct AAssetManager;
struct AAsset;
struct AAssetDir;
enum { AASSET_MODE_UNKNOWN = 0, AASSET_MODE_RANDOM = 1, AASSET_MODE_STREAMING = 2, AASSET_MODE_BUFFER = 3 };
#ifdef __cplusplus
extern "C" {
#endif
AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode);
int AAsset_read(AAsset* asset, void* buf, size_t count);
off_t AAsset_getLength(AAsset* asset);
off_t AAsset_getRemainingLength(AAsset* asset);
void AAsset_close(AAsset* asset);
const void* AAsset_getBuffer(AAsset* asset);
int64_t AAsset_seek(AAsset* asset, int64_t offset, int whence);
int AAsset_openFileDescriptor(AAsset* asset, off_t* outStart, off_t* outLength);
int AAsset_isAllocated(AAsset* asset);
AAssetDir* AAssetManager_openDir(AAssetManager* mgr, const char* dirName);
void AAssetDir_close(AAssetDir* assetDir);
const char* AAssetDir_getNextFileName(AAssetDir* assetDir);
void AAssetDir_rewind(AAssetDir* assetDir);
#ifdef __cplusplus
}
#endif

#include "DemoConfig.h"

#include "scene/Bistro.h"

int main()
{
    minilog::initialize(nullptr, {.threadNames = false});

    MeshData meshData;
    Scene scene;

    loadBistro(meshData, scene);

    LLOGL("Android Bistro cache generated.\n");
    LLOGL("  Meshes:    %s\n", fileNameCachedMeshes);
    LLOGL("  Materials: %s\n", fileNameCachedMaterials);
    LLOGL("  Scene:     %s\n", fileNameCachedHierarchy);
    LLOGL("  Textures:  %s\n", DEMO_TEXTURE_CACHE_FOLDER);
    LLOGL("  Texture format: ASTC KTX with mipmaps\n");

    return 0;
}

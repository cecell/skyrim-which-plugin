# The stock triplet, plus the toolset pin: vcpkg otherwise builds with the runner's newest MSVC,
# not the 14.44 the plugin itself is built and linked with.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)

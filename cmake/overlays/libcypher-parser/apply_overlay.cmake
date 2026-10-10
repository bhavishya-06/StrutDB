# Invoked as PATCH_COMMAND with cwd = populated third-party source tree.
file(COPY "${OVERLAY_DIR}/CMakeLists.txt" DESTINATION ".")
file(COPY "${OVERLAY_DIR}/config.h.in" DESTINATION ".")

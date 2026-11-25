# Source-level fixes applied to the vendored Dawn tree before it is built.
#
# Each patch is an exact string replacement, checked and idempotent, following
# AuroraSDL3Patches.cmake:
#   - included by AuroraDawnProvider.cmake, which calls
#     aurora_dawn_apply_patches() on a pre-provided source tree
#     (FETCHCONTENT_SOURCE_DIR_DAWN);
#   - run as `cmake -DDAWN_SOURCE_DIR=<dir> -P AuroraDawnPatches.cmake` from the
#     FetchContent PATCH_COMMAND for a freshly extracted tarball.

function(_aurora_dawn_replace file description old new)
  if (NOT EXISTS "${file}")
    message(FATAL_ERROR "aurora: Dawn patch '${description}' expects ${file}")
  endif ()
  file(READ "${file}" _content)
  string(FIND "${_content}" "${new}" _already)
  if (NOT _already EQUAL -1)
    return()
  endif ()
  string(FIND "${_content}" "${old}" _anchor)
  if (_anchor EQUAL -1)
    message(FATAL_ERROR "aurora: Dawn patch '${description}' does not apply to ${file}; "
      "the vendored Dawn version changed, review AuroraDawnPatches.cmake")
  endif ()
  string(REPLACE "${old}" "${new}" _content "${_content}")
  file(WRITE "${file}" "${_content}")
  message(STATUS "aurora: Dawn patch applied: ${description}")
endfunction()

function(aurora_dawn_apply_patches dawn_source_dir)
  # FreeBSD uses the same POSIX descriptor and Vulkan paths as Linux in this
  # Dawn revision; there is no separate FreeBSD platform branch yet.
  _aurora_dawn_replace("${dawn_source_dir}/src/utils/platform.h"
    "treat FreeBSD as Linux-like"
    "#elif defined(__linux__)"
    "#elif defined(__linux__) || defined(__FreeBSD__)")
  _aurora_dawn_replace("${dawn_source_dir}/include/dawn/native/VulkanBackend.h"
    "enable Vulkan fd exports on FreeBSD"
    "#if defined(__linux__) || defined(__Fuchsia__)"
    "#if defined(__linux__) || defined(__Fuchsia__) || defined(__FreeBSD__)")
endfunction()

if (DAWN_SOURCE_DIR)
  aurora_dawn_apply_patches("${DAWN_SOURCE_DIR}")
endif ()

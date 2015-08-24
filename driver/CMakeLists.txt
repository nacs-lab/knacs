#

set(KNACS_DRIVER_SRCS
  Kbuild
  nacs_char.c
  knacs.h)

set(KNACS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

set(KNACS_DRIVER_SRCS_LINK)
foreach(f ${KNACS_DRIVER_SRCS})
  set(KNACS_DRIVER_SRCS_LINK ${KNACS_DRIVER_SRCS_LINK}
    "${KNACS_OUTPUT_DIR}/${f}")
  add_custom_command(OUTPUT "${KNACS_OUTPUT_DIR}/${f}"
    COMMAND ln -sf "${CMAKE_CURRENT_SOURCE_DIR}/${f}"
    "${KNACS_OUTPUT_DIR}/${f}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
endforeach()
add_custom_target(nacs-driver-src-link DEPENDS ${KNACS_DRIVER_SRCS_LINK})

add_custom_command(OUTPUT "${KNACS_OUTPUT_DIR}/knacs.ko"
  COMMAND make -C "${KDIR}" "CC=${CMAKE_C_COMPILER}" ARCH=arm
  "M=${KNACS_OUTPUT_DIR}"
  WORKING_DIRECTORY "${KNACS_OUTPUT_DIR}"
  DEPENDS ${KNACS_DRIVER_SRCS} nacs-driver-src-link)

add_custom_target(nacs-driver ALL
  DEPENDS "${KNACS_OUTPUT_DIR}/knacs.ko")

install(FILES "${KNACS_OUTPUT_DIR}/knacs.ko"
  DESTINATION "${EXTRAMODULES_DIR}")

install(FILES "knacs.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/knacs/")

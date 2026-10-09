# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/ton618/esp/esp-idf-6-10-0/esp-idf/components/bootloader/subproject"
  "/home/ton618/placals-firmware/placals/build/bootloader"
  "/home/ton618/placals-firmware/placals/build/bootloader-prefix"
  "/home/ton618/placals-firmware/placals/build/bootloader-prefix/tmp"
  "/home/ton618/placals-firmware/placals/build/bootloader-prefix/src/bootloader-stamp"
  "/home/ton618/placals-firmware/placals/build/bootloader-prefix/src"
  "/home/ton618/placals-firmware/placals/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ton618/placals-firmware/placals/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ton618/placals-firmware/placals/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()

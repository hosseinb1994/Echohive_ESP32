# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/hossein/esp/esp-idf/components/bootloader/subproject"
  "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader"
  "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader-prefix"
  "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader-prefix/tmp"
  "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader-prefix/src/bootloader-stamp"
  "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader-prefix/src"
  "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()

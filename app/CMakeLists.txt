cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drg27_fortissax_ams)

target_sources(app PRIVATE
    src/main.c
)
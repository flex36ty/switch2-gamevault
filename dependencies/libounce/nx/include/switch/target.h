/**
 * @file target.h
 * @brief The platform that these headers are installed for.
 * @note The Switch 2 install (make install-switch2) replaces this file with one
 * that defines __SWITCH2__, so a program that includes the installed headers
 * is built for Switch 2 whatever its compiler flags. In the source tree it
 * defines nothing: the library's own builds choose the platform with
 * -D__SWITCH2__.
 */
#pragma once

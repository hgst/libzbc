# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (c) 2009-2014, HGST, Inc. All rights reserved.
# Copyright (c) 2020 Western Digital Corporation or its affiliates.

bin_PROGRAMS += zbc_finish_zone

zbc_finish_zone_SOURCES = finish_zone/zbc_finish_zone.c
zbc_finish_zone_LDADD = libzone_op.la $(libzbc_ldadd)

dist_man8_MANS += finish_zone/zbc_finish_zone.8

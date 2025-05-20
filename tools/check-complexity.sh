#!/bin/sh

complexity --histogram --score --thresh=3 \
	include/libsocketcangw.h \
	src/libsocketcangw.c

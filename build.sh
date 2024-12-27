#!/bin/bash

cc src/*.c -o hshell -fsanitize=address -Wall -ggdb
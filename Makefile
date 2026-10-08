# halfcraft's tasks. each one wraps a script in tools\ (read it for the details); the real builds are
# vpc + msbuild (half-life side) and gradle (minecraft side). needs gnu make 4+ on windows, e.g.
# `winget install ezwinports.make`, python 3.13+ and windows powershell.
#
#   make setup                     clone both sdk trees and apply halfcraft's patches
#   make build [ENGINE=hl2]        dlls, shaders, dev game folders build\game-<engine>, HalfCraft.exe
#   make mc                        the minecraft mod's jar
#   make mc-run                    the minecraft dev client
#   make mc-test                   the minecraft test client (after test-start): its own run folder and world
#   make run [ENGINE=hl2] [MAP=d1_canals_01] [ARGS="+sv_cheats 1"]
#   make cmd C="'save a' 'load a'" console commands into the running game
#   make test-start [ENGINE=hl2]   set the game's saves and settings aside for tests; make test-stop: back
#   make patches                   write sdk edits back into source\sdk\halfcraft-<engine>.patch
#   make proto                     protocol\halfcraft_protocol.h, minecraft's Proto.java and ProtoStructs.java from protocol\schema
#   make package                   dist\HalfCraft-<version>.zip (NOBUILD=1 packs what's built)
#   make format                    clang-format the c++, ruff the python, eclipse's formatter the java; make lint: check
#                                  (and that the protocol files are what the schema gives)
#   make tidy                      clang-tidy over halfcraft's c++, both engines (after make build)
#
# ENGINE: hl2 (half-life 2's own 32-bit engine), hl2dm (half-life 2: deathmatch's 64-bit one) or all
# (default; `make run` takes hl2dm then). NOPROJECTS=1 skips vpc when no .vpc file changed.

# windows powershell runs the recipes: python calls, with its quoting (make cmd C="'save a' 'load a'")
SHELL := powershell.exe
.SHELLFLAGS := -NoProfile -Command
.DEFAULT_GOAL := help
.PHONY: help setup build mc mc-run mc-test run cmd test-start test-stop patches proto package format lint tidy clean

ENGINE ?= all
RUN_ENGINE := $(if $(filter all,$(ENGINE)),hl2dm,$(ENGINE))
MAP ?=
ARGS ?=
C ?=

help:
	@Get-Content Makefile | Select-Object -First 22 | ForEach-Object { $$_ -replace '^# ?', '' }

setup:
	python tools/build/setup_sdk.py --engine $(ENGINE)

build:
	python tools/build/build_hl2.py --engine $(ENGINE) $(if $(NOPROJECTS),--no-projects)

mc:
	python tools/build/gradle.py build

mc-run:
	python tools/game/test_session.py dev-client

mc-test:
	python tools/game/test_session.py minecraft

run:
	python tools/game/run_hl2.py --engine $(RUN_ENGINE) $(if $(MAP),--map $(MAP)) $(if $(ARGS),-- $(ARGS))

cmd:
	python tools/game/hl2_command.py -- $(C)

test-start:
	python tools/game/test_session.py start --engine $(ENGINE)

test-stop:
	python tools/game/test_session.py stop --engine $(ENGINE)

patches:
	python tools/build/update_patches.py --engine $(ENGINE)

proto:
	python tools/build/protocol.py

package:
	python tools/build/package.py $(if $(NOBUILD),--no-build)

format:
	python tools/lint/format.py
	python tools/build/gradle.py spotlessApply

lint:
	python tools/build/protocol.py --check
	python tools/lint/format.py --check
	python tools/build/gradle.py spotlessCheck

tidy:
	python tools/lint/tidy.py

# build output that's safe to throw away: not build\game-* (saves and settings live there)
clean:
	'build\shaders', 'build\launcher', 'dist' | Where-Object { Test-Path $$_ } | Remove-Item -Recurse -Force

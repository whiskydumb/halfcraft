# halfcraft's tasks. each one wraps a script in tools\ (read it for the details); the real builds are
# vpc + msbuild (half-life side) and gradle (minecraft side). needs gnu make 4+ on windows, e.g.
# `winget install ezwinports.make`, and windows powershell.
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
#   make package                   dist\HalfCraft-<version>.zip (NOBUILD=1 packs what's built)
#   make format                    clang-format halfcraft's c++, eclipse's formatter its java; make lint: check both
#   make tidy                      clang-tidy over halfcraft's c++, both engines (after make build)
#
# ENGINE: hl2 (half-life 2's own 32-bit engine), hl2dm (half-life 2: deathmatch's 64-bit one) or all
# (default; `make run` takes hl2dm then). NOPROJECTS=1 skips vpc when no .vpc file changed.

# 64-bit windows powershell even from a 32-bit make (ezwinports' is): a 32-bit one sees another
# program files and can't read 64-bit processes' paths
SHELL := $(or $(wildcard C:/Windows/Sysnative/WindowsPowerShell/v1.0/powershell.exe),powershell.exe)
.SHELLFLAGS := -NoProfile -ExecutionPolicy Bypass -Command
.DEFAULT_GOAL := help
.PHONY: help setup build mc mc-run mc-test run cmd test-start test-stop patches package format lint tidy clean

ENGINE ?= all
RUN_ENGINE := $(if $(filter all,$(ENGINE)),hl2dm,$(ENGINE))
MAP ?=
ARGS ?=
C ?=

# ARGS as a powershell list: +sv_cheats 1 -> '+sv_cheats','1'
comma := ,
empty :=
space := $(empty) $(empty)
ARGS_LIST := $(subst $(space),$(comma),$(foreach a,$(ARGS),'$(a)'))

help:
	@Get-Content Makefile | Select-Object -First 20 | ForEach-Object { $$_ -replace '^# ?', '' }

setup:
	& ./tools/setup_sdk.ps1 -Engine $(ENGINE)

build:
	& ./tools/build_hl2.ps1 -Engine $(ENGINE) $(if $(NOPROJECTS),-NoProjects)

mc:
	& ./tools/gradle.ps1 build

mc-run:
	& ./tools/test_session.ps1 dev-client

mc-test:
	& ./tools/test_session.ps1 minecraft

run:
	& ./tools/run_hl2.ps1 -Engine $(RUN_ENGINE) $(if $(MAP),-Map $(MAP)) $(if $(ARGS),-Extra $(ARGS_LIST))

cmd:
	& ./tools/hl2_command.ps1 $(C)

test-start:
	& ./tools/test_session.ps1 start -Engine $(ENGINE)

test-stop:
	& ./tools/test_session.ps1 stop -Engine $(ENGINE)

patches:
	& ./tools/update_patches.ps1 -Engine $(ENGINE)

package:
	& ./tools/package.ps1 $(if $(NOBUILD),-NoBuild)

format:
	& ./tools/format.ps1
	& ./tools/gradle.ps1 spotlessApply

lint:
	& ./tools/format.ps1 -Check
	& ./tools/gradle.ps1 spotlessCheck

tidy:
	python tools/tidy.py

# build output that's safe to throw away: not build\game-* (saves and settings live there)
clean:
	'build\shaders', 'build\launcher', 'dist' | Where-Object { Test-Path $$_ } | Remove-Item -Recurse -Force

#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
#
# Installs this repo's git hooks. Run once after cloning.

set -e
root="$(git rev-parse --show-toplevel)"
ln -sf ../../scripts/hooks/pre-commit "$root/.git/hooks/pre-commit"
chmod +x "$root/scripts/hooks/pre-commit"
echo "Installed pre-commit hook (SPDX header check)."

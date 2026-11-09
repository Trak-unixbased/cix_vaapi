# shellcheck shell=sh
# /etc/profile.d/cix-vaapi.sh - installed by the cix-va-driver package.
# Selects the CIX VA-API driver for login shells (SSH, console), unless the
# user already chose another driver.
if [ -z "${LIBVA_DRIVER_NAME:-}" ]; then
    LIBVA_DRIVER_NAME=cix
    export LIBVA_DRIVER_NAME
fi

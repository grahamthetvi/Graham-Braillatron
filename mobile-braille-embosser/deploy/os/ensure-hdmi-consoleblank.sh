#!/usr/bin/env bash
# Keep the kernel from blanking the HDMI framebuffer (DietPi extraargs).
ensure_hdmi_consoleblank() {
  local env_file
  for env_file in /boot/dietpiEnv.txt /boot/firmware/dietpiEnv.txt; do
    [[ -f "${env_file}" ]] || continue
    if grep -q 'consoleblank=0' "${env_file}"; then
      continue
    fi
    if grep -q 'consoleblank=' "${env_file}"; then
      sed -i 's/consoleblank=[0-9][0-9]*/consoleblank=0/' "${env_file}"
      echo "Set consoleblank=0 in ${env_file} so the kernel does not blank HDMI."
      continue
    fi
    if grep -q '^extraargs=' "${env_file}"; then
      sed -i 's/^extraargs=\(.*\)$/extraargs=consoleblank=0 \1/' "${env_file}"
      sed -i '/^extraargs=/s/[[:space:]]*$//' "${env_file}"
    else
      echo 'extraargs=consoleblank=0' >> "${env_file}"
    fi
    echo "Set consoleblank=0 in ${env_file} so the kernel does not blank HDMI."
  done
}

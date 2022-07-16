#!/bin/bash
#
# Copyright (c) 2013-2021 Igor Pecovnik, igor.pecovnik@gma**.com
#
# This file is licensed under the terms of the GNU General Public
# License version 2. This program is licensed "as is" without any
# warranty of any kind, whether express or implied.

[[ -z $VENDOR ]] && VENDOR="Hurakan"
[[ -z $ROOTPWD ]] && ROOTPWD="root" # Must be changed @first login
[[ -z $OPI_USERNAME ]] && OPI_USERNAME="biqu" 
[[ -z $OPI_PWD ]] && OPI_PWD="biqu" 
[[ -z $MAINTAINER ]] && MAINTAINER="Hurakan" # deb signature
[[ -z $MAINTAINERMAIL ]] && MAINTAINERMAIL="leeboby@aliyun.com" # deb signature
[[ -z $DEB_COMPRESS ]] && DEB_COMPRESS="xz" # compress .debs with XZ by default. Use 'none' for faster/larger builds
TZDATA=$(cat /etc/timezone) # Timezone for target is taken from host or defined here.
USEALLCORES=yes # Use all CPU cores for compiling
HOSTRELEASE=$(cat /etc/os-release | grep VERSION_CODENAME | cut -d"=" -f2)
[[ -z $HOSTRELEASE ]] && HOSTRELEASE=$(cut -d'/' -f1 /etc/debian_version)
[[ -z $EXIT_PATCHING_ERROR ]] && EXIT_PATCHING_ERROR="" # exit patching if failed
[[ -z $HOST ]] && HOST="Hurakan" # set hostname to the board
[[ -z $CHINA_DOWNLOAD_MIRROR ]] && CHINA_DOWNLOAD_MIRROR=tsinghua
cd "${SRC}" || exit
[[ -z "${ROOTFSCACHE_VERSION}" ]] && ROOTFSCACHE_VERSION=11
[[ -z "${CHROOT_CACHE_VERSION}" ]] && CHROOT_CACHE_VERSION=7

cd ${SRC}/scripts
BUILD_REPOSITORY_URL=$(improved_git remote get-url $(improved_git remote 2>/dev/null | grep origin) 2>/dev/null)
BUILD_REPOSITORY_COMMIT=$(improved_git describe --match=d_e_a_d_b_e_e_f --always --dirty 2>/dev/null)
ROOTFS_CACHE_MAX=200 # max number of rootfs cache, older ones will be cleaned up

DEB_STORAGE=$DEST/debs
DEB_ORANGEPI=$EXTER/cache/debs

# TODO: fixed name can't be used for parallel image building
ROOT_MAPPER="orangepi-root"
[[ -z $ROOTFS_TYPE ]] && ROOTFS_TYPE=ext4 # default rootfs type is ext4
[[ "ext4 f2fs btrfs xfs nfs fel" != *$ROOTFS_TYPE* ]] && exit_with_error "Unknown rootfs type" "$ROOTFS_TYPE"

[[ -z $BTRFS_COMPRESSION ]] && BTRFS_COMPRESSION=zlib # default btrfs filesystem compression method is zlib
[[ ! $BTRFS_COMPRESSION =~ zlib|lzo|zstd|none ]] && exit_with_error "Unknown btrfs compression method" "$BTRFS_COMPRESSION"

# Fixed image size is in 1M dd blocks (MiB)
# to get size of block device /dev/sdX execute as root:
# echo $(( $(blockdev --getsize64 /dev/sdX) / 1024 / 1024 ))
[[ "f2fs" == *$ROOTFS_TYPE* && -z $FIXED_IMAGE_SIZE ]] && exit_with_error "Please define FIXED_IMAGE_SIZE"

# small SD card with kernel, boot script and .dtb/.bin files
[[ $ROOTFS_TYPE == nfs ]] && FIXED_IMAGE_SIZE=64

# Since we are having too many options for mirror management,
# then here is yet another mirror related option.
# Respecting user's override in case a mirror is unreachable.
case $REGIONAL_MIRROR in
	china)
		[[ -z $USE_MAINLINE_GOOGLE_MIRROR ]] && [[ -z $MAINLINE_MIRROR ]] && MAINLINE_MIRROR=tuna
		[[ -z $USE_GITHUB_UBOOT_MIRROR ]] && [[ -z $UBOOT_MIRROR ]] && UBOOT_MIRROR=gitee
		[[ -z $GITHUB_MIRROR ]] && GITHUB_MIRROR=gitclone
		[[ -z $DOWNLOAD_MIRROR ]] && DOWNLOAD_MIRROR=china
		;;
	*)
		;;
esac


MAINLINE_KERNEL_DIR="$SRC/kernel"
MAINLINE_UBOOT_DIR="$SRC/u-boot"

# Let's set default data if not defined in board configuration above
[[ -z $OFFSET ]] && OFFSET=4 # offset to 1st partition (we use 4MiB boundaries by default)
ARCH=armhf
KERNEL_IMAGE_TYPE=zImage
CAN_BUILD_STRETCH=yes
ATF_COMPILE=yes

[[ -z $CRYPTROOT_SSH_UNLOCK ]] && CRYPTROOT_SSH_UNLOCK=yes
[[ -z $CRYPTROOT_SSH_UNLOCK_PORT ]] && CRYPTROOT_SSH_UNLOCK_PORT=2022
# Default to pdkdf2, this used to be the default with cryptroot <= 2.0, however
# cryptroot 2.1 changed that to Argon2i. Argon2i is a memory intensive
# algorithm which doesn't play well with SBCs (need 1GiB RAM by default !)
# https://gitlab.com/cryptsetup/cryptsetup/-/issues/372
[[ -z $CRYPTROOT_PARAMETERS ]] && CRYPTROOT_PARAMETERS="--pbkdf pbkdf2"
[[ -z $WIREGUARD ]] && WIREGUARD="no"
[[ -z $EXTRAWIFI ]] && EXTRAWIFI="yes"
[[ -z $SKIP_BOOTSPLASH ]] && SKIP_BOOTSPLASH="no"
[[ -z $AUFS ]] && AUFS="yes"
[[ -z $IMAGE_PARTITION_TABLE ]] && IMAGE_PARTITION_TABLE="msdos"
[[ -z $EXTRA_BSP_NAME ]] && EXTRA_BSP_NAME=""
[[ -z $EXTRA_ROOTFS_MIB_SIZE ]] && EXTRA_ROOTFS_MIB_SIZE=0
[[ -z $BUILD_KSRC ]] && BUILD_KSRC="no"

# single ext4 partition is the default and preferred configuration
#BOOTFS_TYPE=''
[[ ! -f ${EXTER}/config/sources/families/$LINUXFAMILY.conf ]] && \
	exit_with_error "Sources configuration not found" "$LINUXFAMILY"

source "${EXTER}/config/sources/families/${LINUXFAMILY}.conf"

# load architecture defaults
source "${EXTER}/config/sources/${ARCH}.conf"

## Extensions: at this point we've sourced all the config files that will be used,
##             and (hopefully) not yet invoked any extension methods. So this is the perfect
##             place to initialize the extension manager. It will create functions
##             like the 'post_family_config' that is invoked below.
initialize_extension_manager

call_extension_method "post_family_config" "config_tweaks_post_family_config" << 'POST_FAMILY_CONFIG'
*give the config a chance to override the family/arch defaults*
This hook is called after the family configuration (`sources/families/xxx.conf`) is sourced.
Since the family can override values from the user configuration and the board configuration,
it is often used to in turn override those.
POST_FAMILY_CONFIG

show_menu() {
	provided_title=$1
	provided_backtitle=$2
	provided_menuname=$3
	# Myy : I don't know why there's a TTY_Y - 8...
	#echo "Provided title : $provided_title"
	#echo "Provided backtitle : $provided_backtitle"
	#echo "Provided menuname : $provided_menuname"
	#echo "Provided options : " "${@:4}"
	#echo "TTY X: $TTY_X Y: $TTY_Y"
	whiptail --title "${provided_title}" --backtitle "${provided_backtitle}" --notags \
                          --menu "${provided_menuname}" "${TTY_Y}" "${TTY_X}" $((TTY_Y - 8))  \
			  "${@:4}" \
			  3>&1 1>&2 2>&3
}

# Myy : FIXME Factorize
show_select_menu() {
	provided_title=$1
	provided_backtitle=$2
	provided_menuname=$3
	#dialog --stdout --title "${provided_title}" --backtitle "${provided_backtitle}" \
	#--checklist "${provided_menuname}" $TTY_Y $TTY_X $((TTY_Y - 8)) "${@:4}"

	#whiptail --separate-output --title "${provided_title}" --backtitle "${provided_backtitle}" \
	#                  --checklist "${provided_menuname}" "${TTY_Y}" "${TTY_X}" $((TTY_Y - 8))  \
	#		  "${@:4}" \
	#		  3>&1 1>&2 2>&3

	whiptail --title "${provided_title}" --backtitle "${provided_backtitle}" \
	                  --checklist "${provided_menuname}" "${TTY_Y}" "${TTY_X}" $((TTY_Y - 8))  \
			  "${@:4}" \
			  3>&1 1>&2 2>&3
}


# Expected variables
# - aggregated_content
# - potential_paths
# - separator
# Write to variables :
# - aggregated_content
aggregate_content() {
	LOG_OUTPUT_FILE="${SRC}/output/${LOG_SUBPATH}/potential-paths.log"
	echo -e "Potential paths :" >> "${LOG_OUTPUT_FILE}"
	show_checklist_variables potential_paths
	for filepath in ${potential_paths}; do
		if [[ -f "${filepath}" ]]; then
			echo -e "${filepath/"$EXTER"\//} yes" >> "${LOG_OUTPUT_FILE}"
			aggregated_content+=$(cat "${filepath}")
			aggregated_content+="${separator}"
#		else
#			echo -e "${filepath/"$EXTER"\//} no\n" >> "${LOG_OUTPUT_FILE}"
		fi

	done
	echo "" >> "${LOG_OUTPUT_FILE}"
	unset LOG_OUTPUT_FILE
}

# set unique mounting directory
MOUNT_UUID=$(uuidgen)
SDCARD="${SRC}/.tmp/rootfs-${MOUNT_UUID}"
MOUNT="${SRC}/.tmp/mount-${MOUNT_UUID}"
DESTIMG="${SRC}/.tmp/image-${MOUNT_UUID}"

[[ -n $ATFSOURCE && -z $ATF_USE_GCC ]] && exit_with_error "Error in configuration: ATF_USE_GCC is unset"
[[ -z $UBOOT_USE_GCC ]] && exit_with_error "Error in configuration: UBOOT_USE_GCC is unset"
[[ -z $KERNEL_USE_GCC ]] && exit_with_error "Error in configuration: KERNEL_USE_GCC is unset"

BOOTCONFIG_VAR_NAME=BOOTCONFIG_${BRANCH^^}
[[ -n ${!BOOTCONFIG_VAR_NAME} ]] && BOOTCONFIG=${!BOOTCONFIG_VAR_NAME}
[[ -z $LINUXCONFIG ]] && LINUXCONFIG="linux-${LINUXFAMILY}-${BRANCH}"
[[ -z $BOOTPATCHDIR ]] && BOOTPATCHDIR="u-boot-$LINUXFAMILY"
[[ -z $ATFPATCHDIR ]] && ATFPATCHDIR="atf-$LINUXFAMILY"
[[ -z $KERNELPATCHDIR ]] && KERNELPATCHDIR="$LINUXFAMILY-$BRANCH"

if [[ "$RELEASE" =~ ^(xenial|bionic|focal|jammy)$ ]]; then
    DISTRIBUTION="Ubuntu"
else
    DISTRIBUTION="Debian"
fi

CLI_CONFIG_PATH="${EXTER}/config/cli/${RELEASE}"
DEBOOTSTRAP_CONFIG_PATH="${CLI_CONFIG_PATH}/debootstrap"

AGGREGATION_SEARCH_ROOT_ABSOLUTE_DIRS="
${EXTER}/config
${EXTER}/config/optional/_any_board/_config
${EXTER}/config/optional/architectures/${ARCH}/_config
${EXTER}/config/optional/families/${LINUXFAMILY}/_config
${EXTER}/config/optional/boards/${BOARD}/_config
${USERPATCHES_PATH}
"

DEBOOTSTRAP_SEARCH_RELATIVE_DIRS="
cli/_all_distributions/debootstrap
cli/${RELEASE}/debootstrap
"

CLI_SEARCH_RELATIVE_DIRS="
cli/_all_distributions/main
cli/${RELEASE}/main
"

PACKAGES_SEARCH_ROOT_ABSOLUTE_DIRS="
${EXTER}/packages
${EXTER}/config/optional/_any_board/_packages
${EXTER}/config/optional/architectures/${ARCH}/_packages
${EXTER}/config/optional/families/${LINUXFAMILY}/_packages
${EXTER}/config/optional/boards/${BOARD}/_packages
"

get_all_potential_paths() {
	local root_dirs="${AGGREGATION_SEARCH_ROOT_ABSOLUTE_DIRS}"
	local rel_dirs="${1}"
	local sub_dirs="${2}"
	local looked_up_subpath="${3}"
	for root_dir in ${root_dirs}; do
		for rel_dir in ${rel_dirs}; do
			for sub_dir in ${sub_dirs}; do
				potential_paths+="${root_dir}/${rel_dir}/${sub_dir}/${looked_up_subpath} "
			done
		done
	done
	# for ppath in ${potential_paths}; do
	#  	echo "Checking for ${ppath}"
	#  	if [[ -f "${ppath}" ]]; then
	#  		echo "OK !|"
	#  	else
	#  		echo "Nope|"
	#  	fi
	# done
}

# Environment variables expected :
# - aggregated_content
# Arguments :
# 1. File to look up in each directory
# 2. The separator to add between each concatenated file
# 3. Relative directories paths added to ${3}
# 4. Relative directories paths added to ${4}
#
# The function will basically generate a list of potential paths by
# generating all the potential paths combinations leading to the
# looked up file
# ${AGGREGATION_SEARCH_ROOT_ABSOLUTE_DIRS}/${3}/${4}/${1}
# Then it will concatenate the content of all the available files
# into ${aggregated_content}
#
# TODO :
# ${4} could be removed by just adding the appropriate paths to ${3}
# dynamically for each case

aggregate_all_root_rel_sub() {
	local separator="${2}"

	local potential_paths=""
	get_all_potential_paths "${3}" "${4}" "${1}"

	aggregate_content
}

aggregate_all_debootstrap() {
	local sub_dirs_to_check=". "
	if [[ ! -z "${SELECTED_CONFIGURATION+x}" ]]; then
		sub_dirs_to_check+="config_${SELECTED_CONFIGURATION}"
	fi
	aggregate_all_root_rel_sub "${1}" "${2}" "${DEBOOTSTRAP_SEARCH_RELATIVE_DIRS}" "${sub_dirs_to_check}"
}

aggregate_all_cli() {
	local sub_dirs_to_check=". "
	if [[ ! -z "${SELECTED_CONFIGURATION+x}" ]]; then
		sub_dirs_to_check+="config_${SELECTED_CONFIGURATION}"
	fi
	aggregate_all_root_rel_sub "${1}" "${2}" "${CLI_SEARCH_RELATIVE_DIRS}" "${sub_dirs_to_check}"
}

one_line() {
	local aggregate_func_name="${1}"
	local aggregated_content=""
	shift 1
	$aggregate_func_name "${@}"
	cleanup_list aggregated_content
}

DEBOOTSTRAP_LIST="$(one_line aggregate_all_debootstrap "packages" " ")"
DEBOOTSTRAP_COMPONENTS="$(one_line aggregate_all_debootstrap "components" " ")"
DEBOOTSTRAP_COMPONENTS="${DEBOOTSTRAP_COMPONENTS// /,}"
PACKAGE_LIST="$(one_line aggregate_all_cli "packages" " ")"
PACKAGE_LIST_ADDITIONAL="$(one_line aggregate_all_cli "packages.additional" " ")"

LOG_OUTPUT_FILE="$SRC/output/${LOG_SUBPATH}/debootstrap-list.log"
show_checklist_variables "DEBOOTSTRAP_LIST DEBOOTSTRAP_COMPONENTS PACKAGE_LIST PACKAGE_LIST_ADDITIONAL PACKAGE_LIST_UNINSTALL"

unset LOG_OUTPUT_FILE

DEBIAN_MIRROR='deb.debian.org/debian'
DEBIAN_SECURTY='security.debian.org/'
UBUNTU_MIRROR='ports.ubuntu.com/'

if [[ $DOWNLOAD_MIRROR == "china" ]] ; then

	if [[ ${CHINA_DOWNLOAD_MIRROR} == tsinghua ]]; then
		DEBIAN_MIRROR='mirrors.tuna.tsinghua.edu.cn/debian'
		DEBIAN_SECURTY='mirrors.tuna.tsinghua.edu.cn/debian-security'
		UBUNTU_MIRROR='mirrors.tuna.tsinghua.edu.cn/ubuntu-ports/'
	fi

	if [[ ${CHINA_DOWNLOAD_MIRROR} == huawai ]]; then
		DEBIAN_MIRROR='repo.huaweicloud.com/debian'
		DEBIAN_SECURTY='repo.huaweicloud.com/debian-security'
		UBUNTU_MIRROR='repo.huaweicloud.com/ubuntu-ports/'
	fi
fi

if [[ $DOWNLOAD_MIRROR == "bfsu" ]] ; then
	DEBIAN_MIRROR='mirrors.bfsu.edu.cn/debian'
	DEBIAN_SECURTY='mirrors.bfsu.edu.cn/debian-security'
	UBUNTU_MIRROR='mirrors.bfsu.edu.cn/ubuntu-ports/'
fi

if [[ "${ARCH}" == "amd64" ]]; then
	UBUNTU_MIRROR='archive.ubuntu.com/ubuntu' # ports are only for non-amd64, of course.

		if [[ -n ${CUSTOM_UBUNTU_MIRROR} ]]; then # ubuntu redirector doesn't work well on amd64
			UBUNTU_MIRROR="${CUSTOM_UBUNTU_MIRROR}"
		fi
fi

# don't use mirrors that throws garbage on 404
if [[ -z ${ARMBIAN_MIRROR} ]]; then
	while true; do

		ARMBIAN_MIRROR=$(wget -SO- -T 1 -t 1 https://redirect.armbian.com 2>&1 | egrep -i "Location" | awk '{print $2}' | head -1)
		[[ ${ARMBIAN_MIRROR} != *armbian.hosthatch* ]] && break

	done
fi


call_extension_method "user_config" << 'USER_CONFIG'
*Invoke function with user override*
Allows for overriding configuration values set anywhere else.
It is called after sourcing the `lib.config` file if it exists,
but before assembling any package lists.
USER_CONFIG

call_extension_method "extension_prepare_config" << 'EXTENSION_PREPARE_CONFIG'
*allow extensions to prepare their own config, after user config is done*
Implementors should preserve variable values pre-set, but can default values an/or validate them.
This runs *after* user_config. Don't change anything not coming from other variables or meant to be configured by the user.
EXTENSION_PREPARE_CONFIG

# apt-cacher-ng mirror configurarion
if [[ $DISTRIBUTION == Ubuntu ]]; then
	APT_MIRROR=$UBUNTU_MIRROR
else
	APT_MIRROR=$DEBIAN_MIRROR
fi

[[ -n $APT_PROXY_ADDR ]] && display_alert "Using custom apt-cacher-ng address" "$APT_PROXY_ADDR" "info"

# Build final package list after possible override
PACKAGE_LIST="$PACKAGE_LIST $PACKAGE_LIST_RELEASE $PACKAGE_LIST_ADDITIONAL"
PACKAGE_MAIN_LIST="$(cleanup_list PACKAGE_LIST)"
PACKAGE_LIST="$(cleanup_list PACKAGE_LIST)"

# remove any packages defined in PACKAGE_LIST_RM in lib.config
aggregated_content="${PACKAGE_LIST_RM} "
aggregate_all_cli "packages.remove" " "
PACKAGE_LIST_RM="$(cleanup_list aggregated_content)"
unset aggregated_content

aggregated_content=""
aggregate_all_cli "packages.uninstall" " "
PACKAGE_LIST_UNINSTALL="$(cleanup_list aggregated_content)"
unset aggregated_content


if [[ -n $PACKAGE_LIST_RM ]]; then
	display_alert "Package remove list ${PACKAGE_LIST_RM}"
	# Turns out that \b can be tricked by dashes.
	# So if you remove mesa-utils but still want to install "mesa-utils-extra"
	# a "\b(mesa-utils)\b" filter will convert "mesa-utils-extra" to "-extra".
	# \W is not tricked by this but consumes the surrounding spaces, so we
	# replace the occurence by one space, to avoid sticking the next word to
	# the previous one after consuming the spaces.
	DEBOOTSTRAP_LIST=$(sed -r "s/\W($(tr ' ' '|' <<< ${PACKAGE_LIST_RM}))\W/ /g" <<< " ${DEBOOTSTRAP_LIST} ")
	PACKAGE_LIST=$(sed -r "s/\W($(tr ' ' '|' <<< ${PACKAGE_LIST_RM}))\W/ /g" <<< " ${PACKAGE_LIST} ")
	PACKAGE_MAIN_LIST=$(sed -r "s/\W($(tr ' ' '|' <<< ${PACKAGE_LIST_RM}))\W/ /g" <<< " ${PACKAGE_MAIN_LIST} ")

	# Removing double spaces... AGAIN, since we might have used a sed on them
	# Do not quote the variables. This would defeat the trick.
	DEBOOTSTRAP_LIST="$(echo ${DEBOOTSTRAP_LIST})"
	PACKAGE_LIST="$(echo ${PACKAGE_LIST})"
	PACKAGE_MAIN_LIST="$(echo ${PACKAGE_MAIN_LIST})"
fi


LOG_OUTPUT_FILE="$SRC/output/${LOG_SUBPATH}/debootstrap-list.log"
echo -e "\nVariables after manual configuration" >>$LOG_OUTPUT_FILE
show_checklist_variables "DEBOOTSTRAP_COMPONENTS DEBOOTSTRAP_LIST PACKAGE_LIST PACKAGE_MAIN_LIST"
unset LOG_OUTPUT_FILE

# Give the option to configure DNS server used in the chroot during the build process
[[ -z $NAMESERVER ]] && NAMESERVER="1.0.0.1" # default is cloudflare alternate

call_extension_method "post_aggregate_packages" "user_config_post_aggregate_packages" << 'POST_AGGREGATE_PACKAGES'
*For final user override, using a function, after all aggregations are done*
Called after aggregating all package lists, before the end of `compilation.sh`.
Packages will still be installed after this is called, so it is the last chance
to confirm or change any packages.
POST_AGGREGATE_PACKAGES

# debug
cat <<-EOF >> "${DEST}"/${LOG_SUBPATH}/output.log

## BUILD SCRIPT ENVIRONMENT

Repository: $REPOSITORY_URL
Version: $REPOSITORY_COMMIT

Host OS: $HOSTRELEASE
Host arch: $(dpkg --print-architecture)
Host system: $(uname -a)
Virtualization type: $(systemd-detect-virt)

## Build script directories
Build directory is located on:
$(findmnt -o TARGET,SOURCE,FSTYPE,AVAIL -T "${SRC}")

Build directory permissions:
$(getfacl -p "${SRC}")

Temp directory permissions:
$(getfacl -p "${SRC}"/.tmp 2> /dev/null)

## BUILD CONFIGURATION

Build target:
Board: $BOARD
Branch: $BRANCH
Minimal: $BUILD_MINIMAL

Kernel configuration:
Repository: $KERNELSOURCE
Branch: $KERNELBRANCH
Config file: $LINUXCONFIG

U-boot configuration:
Repository: $BOOTSOURCE
Branch: $BOOTBRANCH
Config file: $BOOTCONFIG

Partitioning configuration: $IMAGE_PARTITION_TABLE offset: $OFFSET
Boot partition type: ${BOOTFS_TYPE:-(none)} ${BOOTSIZE:+"(${BOOTSIZE} MB)"}
Root partition type: $ROOTFS_TYPE ${FIXED_IMAGE_SIZE:+"(${FIXED_IMAGE_SIZE} MB)"}

CPU configuration: $CPUMIN - $CPUMAX with $GOVERNOR
EOF

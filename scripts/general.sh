#!/bin/bash
#
# Copyright (c) 2015 Igor Pecovnik, igor.pecovnik@gma**.com
#
# This file is licensed under the terms of the GNU General Public
# License version 2. This program is licensed "as is" without any
# warranty of any kind, whether express or implied.


# Functions:
# cleaning
# exit_with_error
# get_package_list_hash
# create_sources_list
# display_alert
# fingerprint_image
# distro_menu
# install_pkg_deb
# prepare_host_basic
# prepare_host
# webseed
# show_developer_warning
# show_checklist_variables

# cleaning <target>
#
# target: what to clean
# "make" - "make clean" for selected kernel and u-boot
# "debs" - delete output/debs for board&branch
# "ubootdebs" - delete output/debs for uboot&board&branch
# "alldebs" - delete output/debs
# "cache" - delete output/cache
# "oldcache" - remove old output/cache
# "images" - delete output/images
# "sources" - delete output/sources
#

cleaning()
{
	case $1 in
		debs) # delete ${DEB_STORAGE} for current branch and family
		if [[ -d "${DEB_STORAGE}" ]]; then
			display_alert "Cleaning ${DEB_STORAGE} for" "$BOARD $BRANCH" "info"
			# easier than dealing with variable expansion and escaping dashes in file names
			find "${DEB_STORAGE}" -name "${CHOSEN_UBOOT}_*.deb" -delete
			find "${DEB_STORAGE}" \( -name "${CHOSEN_KERNEL}_*.deb" -o \
				-name "orangepi-*.deb" -o \
				-name "${CHOSEN_KERNEL/image/dtb}_*.deb" -o \
				-name "${CHOSEN_KERNEL/image/headers}_*.deb" -o \
				-name "${CHOSEN_KERNEL/image/source}_*.deb" -o \
				-name "${CHOSEN_KERNEL/image/firmware-image}_*.deb" \) -delete
			[[ -n $RELEASE ]] && rm -f "${DEB_STORAGE}/${RELEASE}/${CHOSEN_ROOTFS}"_*.deb
			[[ -n $RELEASE ]] && rm -f "${DEB_STORAGE}/${RELEASE}/orangepi-desktop-${RELEASE}"_*.deb
		fi
		;;

		ubootdebs) # delete ${DEB_STORAGE} for uboot, current branch and family
		if [[ -d "${DEB_STORAGE}" ]]; then
			display_alert "Cleaning ${DEB_STORAGE} for u-boot" "$BOARD $BRANCH" "info"
			# easier than dealing with variable expansion and escaping dashes in file names
			find "${DEB_STORAGE}" -name "${CHOSEN_UBOOT}_*.deb" -delete
		fi
		;;

		extras) # delete ${DEB_STORAGE}/extra/$RELEASE for all architectures
		if [[ -n $RELEASE && -d ${DEB_STORAGE}/extra/$RELEASE ]]; then
			display_alert "Cleaning ${DEB_STORAGE}/extra for" "$RELEASE" "info"
			rm -rf "${DEB_STORAGE}/extra/${RELEASE}"
		fi
		;;

		alldebs) # delete output/debs
		[[ -d "${DEB_STORAGE}" ]] && display_alert "Cleaning" "${DEB_STORAGE}" "info" && rm -rf "${DEB_STORAGE}"/*
		;;

		cache) # delete output/cache
		[[ -d $EXTER/cache/rootfs ]] && display_alert "Cleaning" "rootfs cache (all)" "info" && find $EXTER/cache/rootfs -type f -delete
		;;

		images) # delete output/images
		[[ -d "${DEST}"/images ]] && display_alert "Cleaning" "output/images" "info" && rm -rf "${DEST}"/images/*
		;;

		sources) # delete output/sources and output/buildpkg
		[[ -d $EXTER/cache/sources ]] && display_alert "Cleaning" "sources" "info" && rm -rf $EXTER/cache/sources/* "${DEST}"/buildpkg/*
		;;

		oldcache) # remove old `cache/rootfs` except for the newest 8 files
		if [[ -d $EXTER/cache/rootfs && $(ls -1 $EXTER/cache/rootfs/*.lz4 2> /dev/null | wc -l) -gt "${ROOTFS_CACHE_MAX}" ]]; then
			display_alert "Cleaning" "rootfs cache (old)" "info"
			(cd $EXTER/cache/rootfs; ls -t *.lz4 | sed -e "1,${ROOTFS_CACHE_MAX}d" | xargs -d '\n' rm -f)
			# Remove signatures if they are present. We use them for internal purpose
			(cd $EXTER/cache/rootfs; ls -t *.asc | sed -e "1,${ROOTFS_CACHE_MAX}d" | xargs -d '\n' rm -f)
		fi
		;;
	esac
}

# exit_with_error <message> <highlight>
#
# a way to terminate build process
# with verbose error message
#

exit_with_error()
{
	local _file
	local _line=${BASH_LINENO[0]}
	local _function=${FUNCNAME[1]}
	local _description=$1
	local _highlight=$2
	_file=$(basename "${BASH_SOURCE[1]}")
	# local stacktrace="$(get_extension_hook_stracktrace "${BASH_SOURCE[*]}" "${BASH_LINENO[*]}")"

	display_alert "ERROR in function $_function" "$stacktrace" "err"
	display_alert "$_description" "$_highlight" "err"
	display_alert "Process terminated" "" "info"

	if [[ "${ERROR_DEBUG_SHELL}" == "yes" ]]; then
		display_alert "MOUNT" "${MOUNT}" "err"
		display_alert "SDCARD" "${SDCARD}" "err"
		display_alert "Here's a shell." "debug it" "err"
		bash < /dev/tty || true
	fi

	# TODO: execute run_after_build here?
	overlayfs_wrapper "cleanup"
	# unlock loop device access in case of starvation
	exec {FD}>/var/lock/orangepi-debootstrap-losetup
	flock -u "${FD}"

	exit 255
}

# get_package_list_hash
#
# returns md5 hash for current package list and rootfs cache version

get_package_list_hash()
{
	local package_arr exclude_arr
	local list_content
	read -ra package_arr <<< "${DEBOOTSTRAP_LIST} ${PACKAGE_LIST}"
	read -ra exclude_arr <<< "${PACKAGE_LIST_EXCLUDE}"
	( ( printf "%s\n" "${package_arr[@]}"; printf -- "-%s\n" "${exclude_arr[@]}" ) | sort -u; echo "${1}" ) \
		| md5sum | cut -d' ' -f 1
}

# create_sources_list <release> <basedir>
#
# <release>: buster|bullseye|bookworm|bionic|focal|jammy|sid
# <basedir>: path to root directory
#
create_sources_list()
{
	local release=$1
	local basedir=$2
	[[ -z $basedir ]] && exit_with_error "No basedir passed to create_sources_list"

	case $release in

	bullseye|bookworm|trixie)
	cat <<-EOF > "${basedir}"/etc/apt/sources.list
	deb http://${DEBIAN_MIRROR} $release main contrib non-free
	#deb-src http://${DEBIAN_MIRROR} $release main contrib non-free

	deb http://${DEBIAN_MIRROR} ${release}-updates main contrib non-free
	#deb-src http://${DEBIAN_MIRROR} ${release}-updates main contrib non-free

	deb http://${DEBIAN_MIRROR} ${release}-backports main contrib non-free
	#deb-src http://${DEBIAN_MIRROR} ${release}-backports main contrib non-free

	deb http://${DEBIAN_SECURTY} ${release}-security main contrib non-free
	#deb-src http://${DEBIAN_SECURTY} ${release}-security main contrib non-free
	EOF
	;;

	xenial|bionic|focal|impish|jammy)
	cat <<-EOF > "${basedir}"/etc/apt/sources.list
	deb http://${UBUNTU_MIRROR} $release main restricted universe multiverse
	#deb-src http://${UBUNTU_MIRROR} $release main restricted universe multiverse

	deb http://${UBUNTU_MIRROR} ${release}-security main restricted universe multiverse
	#deb-src http://${UBUNTU_MIRROR} ${release}-security main restricted universe multiverse

	deb http://${UBUNTU_MIRROR} ${release}-updates main restricted universe multiverse
	#deb-src http://${UBUNTU_MIRROR} ${release}-updates main restricted universe multiverse

	deb http://${UBUNTU_MIRROR} ${release}-backports main restricted universe multiverse
	#deb-src http://${UBUNTU_MIRROR} ${release}-backports main restricted universe multiverse
	EOF
	;;

	esac
}

#--------------------------------------------------------------------------------------------------------------------------------
# Let's have unique way of displaying alerts
#--------------------------------------------------------------------------------------------------------------------------------
display_alert()
{
	# log function parameters to install.log
	[[ -n "${DEST}" ]] && echo "Displaying message: $@" >> "${DEST}"/${LOG_SUBPATH}/output.log

	local tmp=""
	[[ -n $2 ]] && tmp="[\e[0;33m $2 \x1B[0m]"

	case $3 in
		err)
		echo -e "[\e[0;31m error \x1B[0m] $1 $tmp"
		;;

		wrn)
		echo -e "[\e[0;35m warn \x1B[0m] $1 $tmp"
		;;

		ext)
		echo -e "[\e[0;32m o.k. \x1B[0m] \e[1;32m$1\x1B[0m $tmp"
		;;

		info)
		echo -e "[\e[0;32m o.k. \x1B[0m] $1 $tmp"
		;;

		*)
		echo -e "[\e[0;32m .... \x1B[0m] $1 $tmp"
		;;
	esac
}

#--------------------------------------------------------------------------------------------------------------------------------
# fingerprint_image <out_txt_file> [image_filename]
# Saving build summary to the image
#--------------------------------------------------------------------------------------------------------------------------------
fingerprint_image()
{
	cat <<-EOF > "${1}"
	--------------------------------------------------------------------------------
	Title:			${VENDOR} $REVISION ${BOARD^} $DISTRIBUTION $RELEASE $BRANCH
	Kernel:			Linux $VER
	Build date:		$(date +'%d.%m.%Y')
	Maintainer:		$MAINTAINER <$MAINTAINERMAIL>
	Sources: 		https://github.com/orangepi-xunlong/orangepi-build
	Support: 		http://www.orangepi.org/
	EOF

	if [ -n "$2" ]; then
		cat <<-EOF >> "${1}"
		--------------------------------------------------------------------------------
		Partitioning configuration: $IMAGE_PARTITION_TABLE offset: $OFFSET
		Boot partition type: ${BOOTFS_TYPE:-(none)} ${BOOTSIZE:+"(${BOOTSIZE} MB)"}
		Root partition type: $ROOTFS_TYPE ${FIXED_IMAGE_SIZE:+"(${FIXED_IMAGE_SIZE} MB)"}

		CPU configuration: $CPUMIN - $CPUMAX with $GOVERNOR
		--------------------------------------------------------------------------------
		Verify GPG signature:
		gpg --verify $2.img.asc

		Verify image file integrity:
		sha256sum --check $2.img.sha

		Prepare SD card (four methodes):
		zcat $2.img.gz | pv | dd of=/dev/sdX bs=1M
		dd if=$2.img of=/dev/sdX bs=1M
		balena-etcher $2.img.gz -d /dev/sdX
		balena-etcher $2.img -d /dev/sdX
		EOF
	fi

	cat <<-EOF >> "${1}"
	--------------------------------------------------------------------------------
	$(cat "${SRC}"/LICENSE)
	--------------------------------------------------------------------------------
	EOF
}


#--------------------------------------------------------------------------------------------------------------------------------
# Create kernel boot logo from packages/blobs/splash/logo.png and packages/blobs/splash/spinner.gif (animated)
# and place to the file /lib/firmware/bootsplash
#--------------------------------------------------------------------------------------------------------------------------------
function boot_logo ()
{
display_alert "Building kernel splash logo" "$RELEASE" "info"

	LOGO=${EXTER}/packages/blobs/splash/logo.png
	LOGO_WIDTH=$(identify $LOGO | cut -d " " -f 3 | cut -d x -f 1)
	LOGO_HEIGHT=$(identify $LOGO | cut -d " " -f 3 | cut -d x -f 2)
	THROBBER=${EXTER}/packages/blobs/splash/spinner.gif
	THROBBER_WIDTH=$(identify $THROBBER | head -1 | cut -d " " -f 3 | cut -d x -f 1)
	THROBBER_HEIGHT=$(identify $THROBBER | head -1 | cut -d " " -f 3 | cut -d x -f 2)
	convert -alpha remove -background "#000000"	$LOGO "${SDCARD}"/tmp/logo.rgb
	convert -alpha remove -background "#000000" $THROBBER "${SDCARD}"/tmp/throbber%02d.rgb
	${EXTER}/packages/blobs/splash/bootsplash-packer \
	--bg_red 0x00 \
	--bg_green 0x00 \
	--bg_blue 0x00 \
	--frame_ms 48 \
	--picture \
	--pic_width $LOGO_WIDTH \
	--pic_height $LOGO_HEIGHT \
	--pic_position 0 \
	--blob "${SDCARD}"/tmp/logo.rgb \
	--picture \
	--pic_width $THROBBER_WIDTH \
	--pic_height $THROBBER_HEIGHT \
	--pic_position 0x05 \
	--pic_position_offset 200 \
	--pic_anim_type 1 \
	--pic_anim_loop 0 \
	--blob "${SDCARD}"/tmp/throbber00.rgb \
	--blob "${SDCARD}"/tmp/throbber01.rgb \
	--blob "${SDCARD}"/tmp/throbber02.rgb \
	--blob "${SDCARD}"/tmp/throbber03.rgb \
	--blob "${SDCARD}"/tmp/throbber04.rgb \
	--blob "${SDCARD}"/tmp/throbber05.rgb \
	--blob "${SDCARD}"/tmp/throbber06.rgb \
	--blob "${SDCARD}"/tmp/throbber07.rgb \
	--blob "${SDCARD}"/tmp/throbber08.rgb \
	--blob "${SDCARD}"/tmp/throbber09.rgb \
	--blob "${SDCARD}"/tmp/throbber10.rgb \
	--blob "${SDCARD}"/tmp/throbber11.rgb \
	--blob "${SDCARD}"/tmp/throbber12.rgb \
	--blob "${SDCARD}"/tmp/throbber13.rgb \
	--blob "${SDCARD}"/tmp/throbber14.rgb \
	--blob "${SDCARD}"/tmp/throbber15.rgb \
	--blob "${SDCARD}"/tmp/throbber16.rgb \
	--blob "${SDCARD}"/tmp/throbber17.rgb \
	--blob "${SDCARD}"/tmp/throbber18.rgb \
	--blob "${SDCARD}"/tmp/throbber19.rgb \
	--blob "${SDCARD}"/tmp/throbber20.rgb \
	--blob "${SDCARD}"/tmp/throbber21.rgb \
	--blob "${SDCARD}"/tmp/throbber22.rgb \
	--blob "${SDCARD}"/tmp/throbber23.rgb \
	--blob "${SDCARD}"/tmp/throbber24.rgb \
	--blob "${SDCARD}"/tmp/throbber25.rgb \
	--blob "${SDCARD}"/tmp/throbber26.rgb \
	--blob "${SDCARD}"/tmp/throbber27.rgb \
	--blob "${SDCARD}"/tmp/throbber28.rgb \
	--blob "${SDCARD}"/tmp/throbber29.rgb \
	--blob "${SDCARD}"/tmp/throbber30.rgb \
	--blob "${SDCARD}"/tmp/throbber31.rgb \
	--blob "${SDCARD}"/tmp/throbber32.rgb \
	--blob "${SDCARD}"/tmp/throbber33.rgb \
	--blob "${SDCARD}"/tmp/throbber34.rgb \
	--blob "${SDCARD}"/tmp/throbber35.rgb \
	--blob "${SDCARD}"/tmp/throbber36.rgb \
	--blob "${SDCARD}"/tmp/throbber37.rgb \
	--blob "${SDCARD}"/tmp/throbber38.rgb \
	--blob "${SDCARD}"/tmp/throbber39.rgb \
	--blob "${SDCARD}"/tmp/throbber40.rgb \
	--blob "${SDCARD}"/tmp/throbber41.rgb \
	--blob "${SDCARD}"/tmp/throbber42.rgb \
	--blob "${SDCARD}"/tmp/throbber43.rgb \
	--blob "${SDCARD}"/tmp/throbber44.rgb \
	--blob "${SDCARD}"/tmp/throbber45.rgb \
	--blob "${SDCARD}"/tmp/throbber46.rgb \
	--blob "${SDCARD}"/tmp/throbber47.rgb \
	--blob "${SDCARD}"/tmp/throbber48.rgb \
	--blob "${SDCARD}"/tmp/throbber49.rgb \
	--blob "${SDCARD}"/tmp/throbber50.rgb \
	--blob "${SDCARD}"/tmp/throbber51.rgb \
	--blob "${SDCARD}"/tmp/throbber52.rgb \
	--blob "${SDCARD}"/tmp/throbber53.rgb \
	--blob "${SDCARD}"/tmp/throbber54.rgb \
	--blob "${SDCARD}"/tmp/throbber55.rgb \
	--blob "${SDCARD}"/tmp/throbber56.rgb \
	--blob "${SDCARD}"/tmp/throbber57.rgb \
	--blob "${SDCARD}"/tmp/throbber58.rgb \
	--blob "${SDCARD}"/tmp/throbber59.rgb \
	--blob "${SDCARD}"/tmp/throbber60.rgb \
	--blob "${SDCARD}"/tmp/throbber61.rgb \
	--blob "${SDCARD}"/tmp/throbber62.rgb \
	--blob "${SDCARD}"/tmp/throbber63.rgb \
	--blob "${SDCARD}"/tmp/throbber64.rgb \
	--blob "${SDCARD}"/tmp/throbber65.rgb \
	--blob "${SDCARD}"/tmp/throbber66.rgb \
	--blob "${SDCARD}"/tmp/throbber67.rgb \
	--blob "${SDCARD}"/tmp/throbber68.rgb \
	--blob "${SDCARD}"/tmp/throbber69.rgb \
	--blob "${SDCARD}"/tmp/throbber70.rgb \
	--blob "${SDCARD}"/tmp/throbber71.rgb \
	--blob "${SDCARD}"/tmp/throbber72.rgb \
	--blob "${SDCARD}"/tmp/throbber73.rgb \
	--blob "${SDCARD}"/tmp/throbber74.rgb \
	"${SDCARD}"/lib/firmware/bootsplash.orangepi >/dev/null 2>&1

	# enable additional services
	chroot "${SDCARD}" /bin/bash -c "systemctl --no-reload enable bootsplash-ask-password-console.path >/dev/null 2>&1"
	chroot "${SDCARD}" /bin/bash -c "systemctl --no-reload enable bootsplash-hide-when-booted.service >/dev/null 2>&1"
	chroot "${SDCARD}" /bin/bash -c "systemctl --no-reload enable bootsplash-show-on-shutdown.service >/dev/null 2>&1"
}

DISTRIBUTIONS_DESC_DIR="external/config/distributions"

function distro_menu ()
{
    # create a select menu for choosing a distribution based EXPERT status
	local distrib_dir="${1}"

	if [[ -d "${distrib_dir}" && -f "${distrib_dir}/support" ]]; then
		local support_level="$(cat "${distrib_dir}/support")"
		if [[ "${support_level}" != "supported" && $EXPERT != "yes" ]]; then
			:
		else
			local distro_codename="$(basename "${distrib_dir}")"
			local distro_fullname="$(cat "${distrib_dir}/name")"
			local expert_infos=""
			[[ $EXPERT == "yes" ]] && expert_infos="(${support_level})"

            DISTRIB_TYPE="${DISTRIB_TYPE_CURRENT}"
            [[ -z "${DISTRIB_TYPE_CURRENT}" ]] && DISTRIB_TYPE="bullseye bookworm focal jammy"

			if [[ "${DISTRIB_TYPE}" =~ "${distro_codename}" ]]; then
				options+=("${distro_codename}" "${distro_fullname} ${expert_infos}")
			fi
		fi
	fi
}

function distros_options() {
	for distrib_dir in "${DISTRIBUTIONS_DESC_DIR}/"*; do
		distro_menu "${distrib_dir}"
	done
}

function set_distribution_status() {
	local distro_support_desc_filepath="${SRC}/${DISTRIBUTIONS_DESC_DIR}/${RELEASE}/support"
	if [[ ! -f "${distro_support_desc_filepath}" ]]; then
		exit_with_error "Distribution ${distribution_name} does not exist"
	else
		DISTRIBUTION_STATUS="$(cat "${distro_support_desc_filepath}")"
	fi

	[[ "${DISTRIBUTION_STATUS}" != "supported" ]] && [[ "${EXPERT}" != "yes" ]] && exit_with_error "Orange Pi ${RELEASE} is unsupported and, therefore, only available to experts (EXPERT=yes)"
}

adding_packages()
{
    # add deb files to repository if they are not already there
	display_alert "Checking and adding to repository $release" "$3" "ext"
	for f in "${DEB_STORAGE}${2}"/*.deb
	do
		local name version arch
		name=$(dpkg-deb -I "${f}" | grep Package | awk '{print $2}')
		version=$(dpkg-deb -I "${f}" | grep Version | awk '{print $2}')
		arch=$(dpkg-deb -I "${f}" | grep Architecture | awk '{print $2}')
		# add if not already there
		aptly repo search -architectures="${arch}" -config="${SCRIPTPATH}config/${REPO_CONFIG}" "${1}" 'Name (% '${name}'), $Version (='${version}'), $Architecture (='${arch}')' &>/dev/null
		if [[ $? -ne 0 ]]; then
			display_alert "Adding ${1}" "$name" "info"
			aptly repo add -force-replace=true -config="${SCRIPTPATH}config/${REPO_CONFIG}" "${1}" "${f}" &>/dev/null
		fi
	done
}

# * installation will break if we try to install when package manager is running
wait_for_package_manager()
{
	# exit if package manager is running in the back
	while true; do
		if [[ "$(fuser /var/lib/dpkg/lock 2>/dev/null; echo $?)" != 1 && "$(fuser /var/lib/dpkg/lock-frontend 2>/dev/null; echo $?)" != 1 ]]; then
				display_alert "Package manager is running in the background." "Please wait! Retrying in 30 sec" "wrn"
				sleep 30
			else
				break
		fi
	done
}

# Installing debian packages in the orangepi build system.
# The function accepts four optional parameters:
# autoupdate - If the installation list is not empty then update first.
# upgrade, clean - the same name for apt
# verbose - detailed log for the function
#
# list="pkg1 pkg2 pkg3 pkgbadname pkg-1.0 | pkg-2.0 pkg5 (>= 9)"
# install_pkg_deb upgrade verbose $list
# or
# install_pkg_deb autoupdate $list
#
# If the package has a bad name, we will see it in the log file.
# If there is an LOG_OUTPUT_FILE variable and it has a value as
# the full real path to the log file, then all the information will be there.
#
# The LOG_OUTPUT_FILE variable must be defined in the calling function
# before calling the install_pkg_deb function and unset after.
#
install_pkg_deb ()
{
	local list=""
	local log_file
	local for_install
	local need_autoup=false
	local need_upgrade=false
	local need_clean=false
	local need_verbose=false
	local _line=${BASH_LINENO[0]}
	local _function=${FUNCNAME[1]}
	local _file=$(basename "${BASH_SOURCE[1]}")
	local tmp_file=$(mktemp /tmp/install_log_XXXXX)
	export DEBIAN_FRONTEND=noninteractive

	list=$(
	for p in $*;do
		case $p in
			autoupdate) need_autoup=true; continue ;;
			upgrade) need_upgrade=true; continue ;;
			clean) need_clean=true; continue ;;
			verbose) need_verbose=true; continue ;;
			\||\(*|*\)) continue ;;
		esac
		echo " $p"
	done
	)

	if [ -d $(dirname $LOG_OUTPUT_FILE) ]; then
		log_file=${LOG_OUTPUT_FILE}
	else
		log_file="${SRC}/output/${LOG_SUBPATH}/install.log"
	fi

	# This is necessary first when there is no apt cache.
	if $need_upgrade; then
		apt-get -q update || echo "apt cannot update" >>$tmp_file
		apt-get -y upgrade || echo "apt cannot upgrade" >>$tmp_file
	fi

	# If the package is not installed, check the latest
	# up-to-date version in the apt cache.
	# Exclude bad package names and send a message to the log.
	for_install=$(
	for p in $list;do
	  if $(dpkg-query -W -f '${db:Status-Abbrev}' $p |& awk '/ii/{exit 1}');then
		apt-cache  show $p -o APT::Cache::AllVersions=no |& \
		awk -v p=$p -v tmp_file=$tmp_file \
		'/^Package:/{print $2} /^E:/{print "Bad package name: ",p >>tmp_file}'
	  fi
	done
	)

	# This information should be logged.
	if [ -s $tmp_file ]; then
		echo -e "\nInstalling packages in function: $_function" "[$_file:$_line]" \
		>>$log_file
		echo -e "\nIncoming list:" >>$log_file
		printf "%-30s %-30s %-30s %-30s\n" $list >>$log_file
		echo "" >>$log_file
		cat $tmp_file >>$log_file
	fi

	if [ -n "$for_install" ]; then
		if $need_autoup; then
			apt-get -q update
			apt-get -y upgrade
		fi
		apt-get install -qq -y --no-install-recommends $for_install
		echo -e "\nPackages installed:" >>$log_file
		dpkg-query -W \
		  -f '${binary:Package;-27} ${Version;-23}\n' \
		  $for_install >>$log_file
	fi

	# We will show the status after installation all listed
	if $need_verbose; then
		echo -e "\nstatus after installation:" >>$log_file
		dpkg-query -W \
		  -f '${binary:Package;-27} ${Version;-23} [ ${Status} ]\n' \
		  $list >>$log_file
	fi

	if $need_clean;then apt-get clean; fi
	rm $tmp_file
}

prepare_host_basic()
{
	# command:package1 package2 ...
	# list of commands that are neeeded:packages where this command is
	local check_pack install_pack
	local checklist=(
			"whiptail:whiptail"
			"dialog:dialog"
			"fuser:psmisc"
			"getfacl:acl"
			"uuid:uuid uuid-runtime"
			"curl:curl"
			"gpg:gnupg"
			"gawk:gawk"
			"git:git"
			)

	for check_pack in "${checklist[@]}"; do
	    if ! which ${check_pack%:*} >/dev/null; then local install_pack+=${check_pack#*:}" "; fi
	done

	if [[ -n $install_pack ]]; then
		display_alert "Installing basic packages" "$install_pack"
		sudo bash -c "apt-get -qq update && apt-get install -qq -y --no-install-recommends $install_pack"
	fi

}

# * checks and installs necessary packages
# * creates directory structure
# * changes system settings
prepare_host()
{
	display_alert "Preparing" "host" "info"

	# wait until package manager finishes possible system maintanace
	wait_for_package_manager

	# fix for Locales settings
	if ! grep -q "^en_US.UTF-8 UTF-8" /etc/locale.gen; then
		sudo sed -i 's/# en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen
		sudo locale-gen
	fi

	export LC_ALL="en_US.UTF-8"

	# packages list for host
	# NOTE: please sync any changes here with the Dockerfile and Vagrantfile
	local hostdeps="acl aptly aria2 bc binfmt-support bison btrfs-progs       \
	build-essential  ca-certificates ccache cpio cryptsetup curl              \
	debian-archive-keyring debian-keyring debootstrap device-tree-compiler    \
	dialog dirmngr dosfstools dwarves f2fs-tools fakeroot flex gawk           \
	gcc-arm-linux-gnueabihf gdisk gnupg1 gpg imagemagick jq kmod libbison-dev \
	libc6-dev-armhf-cross libelf-dev libfdt-dev libfile-fcntllock-perl        \
	libfl-dev liblz4-tool libncurses-dev libpython2.7-dev libssl-dev          \
	libusb-1.0-0-dev linux-base locales lzop ncurses-base ncurses-term        \
	nfs-kernel-server ntpdate p7zip-full parted patchutils pigz pixz          \
	pkg-config pv python3-dev python3-distutils qemu-user-static rsync swig   \
	systemd-container u-boot-tools udev unzip uuid-dev wget whiptail zip      \
	zlib1g-dev"

    hostdeps+=" distcc lib32ncurses-dev lib32stdc++6 libc6-i386"
    grep -q i386 <(dpkg --print-foreign-architectures) || dpkg --add-architecture i386

	# Add support for Ubuntu 20.04, 21.04 and Mint 20.x
	if [[ $HOSTRELEASE =~ ^(focal|ulyana|ulyssa|bullseye|bookworm|uma)$ ]]; then
		hostdeps+=" python2 python3"
		ln -fs /usr/bin/python2.7 /usr/bin/python2
		ln -fs /usr/bin/python2.7 /usr/bin/python
	else
		hostdeps+=" python libpython-dev"
	fi

	display_alert "Build host OS release" "${HOSTRELEASE:-(unknown)}" "info"

	export EXTRA_BUILD_DEPS=""
	if [ -n "${EXTRA_BUILD_DEPS}" ]; then hostdeps+=" ${EXTRA_BUILD_DEPS}"; fi

	display_alert "Installing build dependencies"

	sudo echo "apt-cacher-ng    apt-cacher-ng/tunnelenable      boolean false" | sudo debconf-set-selections

	LOG_OUTPUT_FILE="${DEST}"/${LOG_SUBPATH}/hostdeps.log
	install_pkg_deb "autoupdate $hostdeps"
	unset LOG_OUTPUT_FILE

	update-ccache-symlinks

	export FINAL_HOST_DEPS="$hostdeps ${EXTRA_BUILD_DEPS}"

	# sync clock
    display_alert "Syncing clock" "${NTP_SERVER:-pool.ntp.org}" "info"
    ntpdate -s "${NTP_SERVER:-pool.ntp.org}"

	# create directory structure
	mkdir -p $SRC/output $EXTER/cache $USERPATCHES_PATH
	if [[ -n $SUDO_USER ]]; then
		chgrp --quiet sudo cache output "${USERPATCHES_PATH}"
		# SGID bit on cache/sources breaks kernel dpkg packaging
		chmod --quiet g+w,g+s output "${USERPATCHES_PATH}"
		# fix existing permissions
		find "${SRC}"/output "${USERPATCHES_PATH}" -type d ! -group sudo -exec chgrp --quiet sudo {} \;
		find "${SRC}"/output "${USERPATCHES_PATH}" -type d ! -perm -g+w,g+s -exec chmod --quiet g+w,g+s {} \;
	fi
	mkdir -p $DEST/debs/{extra,u-boot}  $DEST/{config,debug,patch,images} $USERPATCHES_PATH/overlay $EXTER/cache/{debs,sources,hash} $SRC/toolchains  $SRC/.tmp

    # bind mount toolchain if defined
    if [[ -d "${ARMBIAN_CACHE_TOOLCHAIN_PATH}" ]]; then
        mountpoint -q "${SRC}"/cache/toolchain && umount -l "${SRC}"/cache/toolchain
        mount --bind "${ARMBIAN_CACHE_TOOLCHAIN_PATH}" "${SRC}"/cache/toolchain
    fi

    display_alert "Checking for external GCC compilers" "" "info"
    # download external Linaro compiler and missing special dependencies since they are needed for certain sources

    local toolchains=(
        "gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu.tar.xz"
        )

    USE_TORRENT_STATUS=${USE_TORRENT}
    USE_TORRENT="no"

    cd $SRC/toolchains/gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/
    sudo cat libexec.tar.gz* | sudo tar zx

    USE_TORRENT=${USE_TORRENT_STATUS}

    rm -rf $SRC/toolchains/*.tar.xz*
    local existing_dirs=( $(ls -1 $SRC/toolchains) )
    for dir in ${existing_dirs[@]}; do
        local found=no
        for toolchain in ${toolchains[@]}; do
            local filename=${toolchain##*/}
            local dirname=${filename//.tar.xz}
            [[ $dir == $dirname ]] && found=yes
        done
        if [[ $found == no ]]; then
            display_alert "Removing obsolete toolchain" "$dir"
            rm -rf $SRC/toolchains/$dir
        fi
    done

	# enable arm binary format so that the cross-architecture chroot environment will work
	if [[ $BUILD_OPT == "image" || $BUILD_OPT == "rootfs" ]]; then
		modprobe -q binfmt_misc
		mountpoint -q /proc/sys/fs/binfmt_misc/ || mount binfmt_misc -t binfmt_misc /proc/sys/fs/binfmt_misc
		if [[ "$(arch)" != "aarch64" ]]; then
			test -e /proc/sys/fs/binfmt_misc/qemu-arm || update-binfmts --enable qemu-arm
			test -e /proc/sys/fs/binfmt_misc/qemu-aarch64 || update-binfmts --enable qemu-aarch64
		fi
	fi

	# check free space (basic)
	local freespace=$(findmnt --target "${SRC}" -n -o AVAIL -b 2>/dev/null) # in bytes
	if [[ -n $freespace && $(( $freespace / 1073741824 )) -lt 10 ]]; then
		display_alert "Low free space left" "$(( $freespace / 1073741824 )) GiB" "wrn"
		# pause here since dialog-based menu will hide this message otherwise
		echo -e "Press \e[0;33m<Ctrl-C>\x1B[0m to abort compilation, \e[0;33m<Enter>\x1B[0m to ignore and continue"
		read
	fi
}

function webseed ()
{
	# list of mirrors that host our files
	unset text
	# Hardcoded to EU mirrors since
	local CCODE=$(curl -s redirect.armbian.com/geoip | jq '.continent.code' -r)
	# WEBSEED=($(curl -s https://redirect.armbian.com/mirrors | jq -r '.'${CCODE}' | .[] | values'))
	# aria2 simply split chunks based on sources count not depending on download speed
	# when selecting china mirrors, use only China mirror, others are very slow there
    WEBSEED=(https://mirrors.tuna.tsinghua.edu.cn/armbian-releases/)

	for toolchain in ${WEBSEED[@]}; do
		text="${text} ${toolchain}${1}"
	done
	text="${text:1}"
	echo "${text}"
}

show_developer_warning()
{
	local temp_rc
	temp_rc=$(mktemp)
	cat <<-'EOF' > "${temp_rc}"
	screen_color = (WHITE,RED,ON)
	EOF
	local warn_text="You are switching to the \Z1EXPERT MODE\Zn

	This allows building experimental configurations that are provided
	\Z1AS IS\Zn to developers and expert users,
	\Z1WITHOUT ANY RESPONSIBILITIES\Zn from the Armbian team:

	- You are using these configurations \Z1AT YOUR OWN RISK\Zn
	- Bug reports related to the dev kernel, CSC, WIP and EOS boards
	\Z1will be closed without a discussion\Zn
	- Forum posts related to dev kernel, CSC, WIP and EOS boards
	should be created in the \Z2\"Community forums\"\Zn section
	"
	DIALOGRC=$temp_rc dialog --title "Expert mode warning" --backtitle "${backtitle}" --colors --defaultno --no-label "I do not agree" \
		--yes-label "I understand and agree" --yesno "$warn_text" "${TTY_Y}" "${TTY_X}"
	[[ $? -ne 0 ]] && exit_with_error "Error switching to the expert mode"
	SHOW_WARNING=no
}

# is a formatted output of the values of variables
# from the list at the place of the function call.
#
# The LOG_OUTPUT_FILE variable must be defined in the calling function
# before calling the `show_checklist_variables` function and unset after.
#
show_checklist_variables ()
{
	local checklist=$*
	local var pval
	local log_file=${LOG_OUTPUT_FILE:-"${SRC}"/output/${LOG_SUBPATH}/trash.log}
	local _line=${BASH_LINENO[0]}
	local _function=${FUNCNAME[1]}
	local _file=$(basename "${BASH_SOURCE[1]}")

	echo -e "Show variables in function: $_function" "[$_file:$_line]\n" >>$log_file

	for var in $checklist;do
		eval pval=\$$var
		echo -e "\n$var =:" >>$log_file
		if [ $(echo "$pval" | awk -F"/" '{print NF}') -ge 4 ];then
			printf "%s\n" $pval >>$log_file
		else
			printf "%-30s %-30s %-30s %-30s\n" $pval >>$log_file
		fi
	done
}


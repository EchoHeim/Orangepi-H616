#!/bin/bash
#
# Copyright (c) 2013-2021 Igor Pecovnik, igor.pecovnik@gma**.com
#
# This file is licensed under the terms of the GNU General Public
# License version 2. This program is licensed "as is" without any
# warranty of any kind, whether express or implied.

# Functions:

# compile_atf
# compile_uboot
# compile_kernel
# compile_firmware
# compile_orangepi-config
# compile_sunxi_tools
# grab_version
# find_toolchain
# advanced_patch
# process_patch_file
# userpatch_create
# overlayfs_wrapper

compile_atf()
{
	if [[ $CLEAN_LEVEL == *make* ]]; then
		display_alert "Cleaning" "$ATFSOURCEDIR" "info"
		(cd "${EXTER}/cache/sources/${ATFSOURCEDIR}"; make distclean > /dev/null 2>&1)
	fi

	cd "$EXTER/cache/sources/$ATFSOURCEDIR" 

	display_alert "Compiling ATF" "" "info"

    local toolchain
    toolchain=$(find_toolchain "$ATF_COMPILER" "$ATF_USE_GCC")
    echo "toolchain: $toolchain"
    [[ -z $toolchain ]] && exit_with_error "Could not find required toolchain" "${ATF_COMPILER}gcc $ATF_USE_GCC"

    if [[ -n $ATF_TOOLCHAIN2 ]]; then
        local toolchain2_type toolchain2_ver toolchain2
        toolchain2_type=$(cut -d':' -f1 <<< "${ATF_TOOLCHAIN2}")
        toolchain2_ver=$(cut -d':' -f2 <<< "${ATF_TOOLCHAIN2}")
        toolchain2=$(find_toolchain "$toolchain2_type" "$toolchain2_ver")
        [[ -z $toolchain2 ]] && exit_with_error "Could not find required toolchain" "${toolchain2_type}gcc $toolchain2_ver"
    fi

	display_alert "Compiler version" "${ATF_COMPILER}gcc $(eval env PATH="${toolchain}:${PATH}" "${ATF_COMPILER}gcc" -dumpversion)" "info"

	local target_make target_patchdir target_files
	target_make=$(cut -d';' -f1 <<< "${ATF_TARGET_MAP}")
	target_patchdir=$(cut -d';' -f2 <<< "${ATF_TARGET_MAP}")
	target_files=$(cut -d';' -f3 <<< "${ATF_TARGET_MAP}")

	advanced_patch "atf" "${ATFPATCHDIR}" "$BOARD" "$target_patchdir" "$BRANCH" "${LINUXFAMILY}-${BOARD}-${BRANCH}"

	# create patch for manual source changes
	[[ $CREATE_PATCHES == yes ]] && userpatch_create "atf"

	echo -e "\n\t==  atf  ==\n" >> "${DEST}"/${LOG_SUBPATH}/compilation.log
	# ENABLE_BACKTRACE="0" has been added to workaround a regression in ATF.
	# Check: https://github.com/armbian/build/issues/1157
	eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${toolchain2}:${PATH}" \
		'make ENABLE_BACKTRACE="0" $target_make $CTHREADS \
		CROSS_COMPILE="$CCACHE $ATF_COMPILER"' 2>> "${DEST}"/${LOG_SUBPATH}/compilation.log \
		${PROGRESS_LOG_TO_FILE:+' | tee -a $DEST/${LOG_SUBPATH}/compilation.log'} \
		${OUTPUT_DIALOG:+' | dialog --backtitle "$backtitle" --progressbox "Compiling ATF..." $TTY_Y $TTY_X'} \
		${OUTPUT_VERYSILENT:+' >/dev/null 2>/dev/null'}

	[[ ${PIPESTATUS[0]} -ne 0 ]] && exit_with_error "ATF compilation failed"

	[[ $(type -t atf_custom_postprocess) == function ]] && atf_custom_postprocess

	atftempdir=$(mktemp -d)
	chmod 700 ${atftempdir}
	trap "rm -rf \"${atftempdir}\" ; exit 0" 0 1 2 3 15

	# copy files to temp directory
	for f in $target_files; do
		local f_src
		f_src=$(cut -d':' -f1 <<< "${f}")
		if [[ $f == *:* ]]; then
			local f_dst
			f_dst=$(cut -d':' -f2 <<< "${f}")
		else
			local f_dst
			f_dst=$(basename "${f_src}")
		fi
		[[ ! -f $f_src ]] && exit_with_error "ATF file not found" "$(basename "${f_src}")"
		cp "${f_src}" "${atftempdir}/${f_dst}"
	done

	# copy license file to pack it to u-boot package later
	[[ -f license.md ]] && cp license.md "${atftempdir}"/
}

compile_uboot()
{
	# not optimal, but extra cleaning before overlayfs_wrapper should keep sources directory clean
	if [[ $CLEAN_LEVEL == *make* ]]; then
		display_alert "Cleaning" "$BOOTSOURCEDIR" "info"
		(cd $BOOTSOURCEDIR; make clean > /dev/null 2>&1)
	fi

	local ubootdir="$BOOTSOURCEDIR"

	cd "${ubootdir}" || exit

	local version
	version="2021.10"

	display_alert "Compiling u-boot" "v$version" "info"

    local toolchain
    toolchain=$(find_toolchain "$UBOOT_COMPILER" "$UBOOT_USE_GCC")
    [[ -z $toolchain ]] && exit_with_error "Could not find required toolchain" "${UBOOT_COMPILER}gcc $UBOOT_USE_GCC"

    if [[ -n $UBOOT_TOOLCHAIN2 ]]; then
        local toolchain2_type toolchain2_ver toolchain2
        toolchain2_type=$(cut -d':' -f1 <<< "${UBOOT_TOOLCHAIN2}")
        toolchain2_ver=$(cut -d':' -f2 <<< "${UBOOT_TOOLCHAIN2}")
        toolchain2=$(find_toolchain "$toolchain2_type" "$toolchain2_ver")
        [[ -z $toolchain2 ]] && exit_with_error "Could not find required toolchain" "${toolchain2_type}gcc $toolchain2_ver"
    fi

	display_alert "Compiler version" "${UBOOT_COMPILER}gcc $(eval env PATH="${toolchain}:${toolchain2}:${PATH}" "${UBOOT_COMPILER}gcc" -dumpversion)" "info"
	[[ -n $toolchain2 ]] && display_alert "Additional compiler version" "${toolchain2_type}gcc $(eval env PATH="${toolchain}:${toolchain2}:${PATH}" "${toolchain2_type}gcc" -dumpversion)" "info"

	# create directory structure for the .deb package
	uboottempdir=$(mktemp -d)
	chmod 700 ${uboottempdir}
	trap "ret=\$?; rm -rf \"${uboottempdir}\" ; exit \$ret" 0 1 2 3 15
	local uboot_name=${CHOSEN_UBOOT}_${REVISION}_${ARCH}
	rm -rf $uboottempdir/$uboot_name
	mkdir -p $uboottempdir/$uboot_name/usr/lib/{u-boot,$uboot_name} $uboottempdir/$uboot_name/DEBIAN

	# process compilation for one or multiple targets
	while read -r target; do
		local target_make target_patchdir target_files
		target_make=$(cut -d';' -f1 <<< "${target}")
		target_patchdir=$(cut -d';' -f2 <<< "${target}")
		target_files=$(cut -d';' -f3 <<< "${target}")

		if [[ $CLEAN_LEVEL == *make* ]]; then
			display_alert "Cleaning" "$BOOTSOURCEDIR" "info"
			(cd "${BOOTSOURCEDIR}"; make clean > /dev/null 2>&1)
		fi

		advanced_patch "u-boot" "$BOOTPATCHDIR" "$BOARD" "$target_patchdir" "$BRANCH" "${LINUXFAMILY}-${BOARD}-${BRANCH}"

		# create patch for manual source changes
		[[ $CREATE_PATCHES == yes ]] && userpatch_create "u-boot"

		if [[ -n $ATFSOURCE ]]; then
			cp -Rv "${atftempdir}"/*.bin .
			rm -rf "${atftempdir}"
		fi

		echo -e "\n\t== u-boot make $BOOTCONFIG ==\n" >> "${DEST}"/${LOG_SUBPATH}/compilation.log
		eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${toolchain2}:${PATH}" \
			'make $CTHREADS $BOOTCONFIG \
			CROSS_COMPILE="$CCACHE $UBOOT_COMPILER"' 2>> "${DEST}"/${LOG_SUBPATH}/compilation.log \
			${PROGRESS_LOG_TO_FILE:+' | tee -a $DEST/${LOG_SUBPATH}/compilation.log'} \
			${OUTPUT_VERYSILENT:+' >/dev/null 2>/dev/null'}

        # orangepi specifics u-boot settings
        [[ -f .config ]] && sed -i 's/CONFIG_LOCALVERSION=""/CONFIG_LOCALVERSION="-orangepi"/g' .config
        [[ -f .config ]] && sed -i 's/CONFIG_LOCALVERSION_AUTO=.*/# CONFIG_LOCALVERSION_AUTO is not set/g' .config

        [[ -f tools/logos/udoo.bmp ]] && cp "${EXTER}"/packages/blobs/splash/udoo.bmp tools/logos/udoo.bmp
        touch .scmversion

        # $BOOTDELAY can be set in board family config, ensure autoboot can be stopped even if set to 0
        [[ $BOOTDELAY == 0 ]] && echo -e "CONFIG_ZERO_BOOTDELAY_CHECK=y" >> .config
        [[ -n $BOOTDELAY ]] && sed -i "s/^CONFIG_BOOTDELAY=.*/CONFIG_BOOTDELAY=${BOOTDELAY}/" .config || [[ -f .config ]] && echo "CONFIG_BOOTDELAY=${BOOTDELAY}" >> .config

		# workaround when two compilers are needed
		cross_compile="CROSS_COMPILE=$CCACHE $UBOOT_COMPILER";
		[[ -n $UBOOT_TOOLCHAIN2 ]] && cross_compile="ORANGEPI=foe"; # empty parameter is not allowed

		echo -e "\n\t== u-boot make $target_make ==\n" >> "${DEST}"/${LOG_SUBPATH}/compilation.log
		eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${toolchain2}:${PATH}" \
			'make $target_make $CTHREADS \
			"${cross_compile}"' 2>>"${DEST}"/${LOG_SUBPATH}/compilation.log \
			${PROGRESS_LOG_TO_FILE:+' | tee -a "${DEST}"/${LOG_SUBPATH}/compilation.log'} \
			${OUTPUT_DIALOG:+' | dialog --backtitle "$backtitle" --progressbox "Compiling u-boot..." $TTY_Y $TTY_X'} \
			${OUTPUT_VERYSILENT:+' >/dev/null 2>/dev/null'} ';EVALPIPE=(${PIPESTATUS[@]})'

		[[ ${EVALPIPE[0]} -ne 0 ]] && exit_with_error "U-boot compilation failed"

		[[ $(type -t uboot_custom_postprocess) == function ]] && uboot_custom_postprocess

		# copy files to build directory
		for f in $target_files; do
			local f_src
			f_src=$(cut -d':' -f1 <<< "${f}")
			if [[ $f == *:* ]]; then
				local f_dst
				f_dst=$(cut -d':' -f2 <<< "${f}")
			else
				local f_dst
				f_dst=$(basename "${f_src}")
			fi
			[[ ! -f $f_src ]] && exit_with_error "U-boot file not found" "$(basename "${f_src}")"
			cp "${f_src}" "$uboottempdir/${uboot_name}/usr/lib/${uboot_name}/${f_dst}"
		done
	done <<< "$UBOOT_TARGET_MAP"

	if [[ $PACK_UBOOT == "yes" ]];then
        pack_uboot
        cp $uboottempdir/packout/{boot0_sdcard.fex,boot_package.fex} "${SRC}/.tmp/${uboot_name}/usr/lib/${uboot_name}/"
        cp $uboottempdir/packout/dts/${BOARD}-u-boot.dts "${SRC}/.tmp/${uboot_name}/usr/lib/u-boot/"

		cd "${ubootdir}" || exit
	fi

	# declare -f on non-defined function does not do anything
	cat <<-EOF > "$uboottempdir/${uboot_name}/usr/lib/u-boot/platform_install.sh"
	DIR=/usr/lib/$uboot_name
	$(declare -f write_uboot_platform)
	$(declare -f write_uboot_platform_mtd)
	$(declare -f setup_write_uboot_platform)
	EOF

	# set up control file
	cat <<-EOF > "$uboottempdir/${uboot_name}/DEBIAN/control"
	Package: linux-u-boot-${BOARD}-${BRANCH}
	Version: $REVISION
	Architecture: $ARCH
	Maintainer: $MAINTAINER <$MAINTAINERMAIL>
	Installed-Size: 1
	Section: kernel
	Priority: optional
	Provides: orangepi-u-boot
	Replaces: orangepi-u-boot
	Conflicts: orangepi-u-boot, u-boot-sunxi
	Description: Uboot loader $version
	EOF

	# copy config file to the package
	# useful for FEL boot with overlayfs_wrapper
	[[ -f .config && -n $BOOTCONFIG ]] && cp .config "$uboottempdir/${uboot_name}/usr/lib/u-boot/${BOOTCONFIG}"
	# copy license files from typical locations
	[[ -f COPYING ]] && cp COPYING "$uboottempdir/${uboot_name}/usr/lib/u-boot/LICENSE"
	[[ -f Licenses/README ]] && cp Licenses/README "$uboottempdir/${uboot_name}/usr/lib/u-boot/LICENSE"
	[[ -n $atftempdir && -f $atftempdir/license.md ]] && cp "${atftempdir}/license.md" "$uboottempdir/${uboot_name}/usr/lib/u-boot/LICENSE.atf"

	display_alert "Building deb" "${uboot_name}.deb" "info"
	fakeroot dpkg-deb -Z xz -b "$uboottempdir/${uboot_name}" "$uboottempdir/${uboot_name}.deb" >> "${DEST}"/${LOG_SUBPATH}/output.log 2>&1
	rm -rf "$uboottempdir/${uboot_name}"
	[[ -n $atftempdir ]] && rm -rf "${atftempdir}"

	[[ ! -f $uboottempdir/${uboot_name}.deb ]] && exit_with_error "Building u-boot package failed"

	rsync --remove-source-files -rq "$uboottempdir/${uboot_name}.deb" "${DEB_STORAGE}/u-boot/"
	rm -rf "$uboottempdir"
}

create_linux-source_package ()
{
	ts=$(date +%s)
	local sources_pkg_dir tmp_src_dir
	tmp_src_dir=$(mktemp -d)
	trap "ret=\$?; rm -rf \"${tmp_src_dir}\" ; exit \$ret" 0 1 2 3 15
	sources_pkg_dir=${tmp_src_dir}/${CHOSEN_KSRC}_${REVISION}_all
	mkdir -p "${sources_pkg_dir}"/usr/src/ \
		"${sources_pkg_dir}"/usr/share/doc/linux-source-${version}-${LINUXFAMILY} \
		"${sources_pkg_dir}"/DEBIAN

	cp "${EXTER}/config/kernel/${LINUXCONFIG}.config" "default_${LINUXCONFIG}.config"
	xz < .config > "${sources_pkg_dir}/usr/src/${LINUXCONFIG}_${version}_${REVISION}_config.xz"

	display_alert "Compressing sources for the linux-source package"
	tar cp --directory="$kerneldir" --exclude='.git' --owner=root . \
		| pv -p -b -r -s "$(du -sb "$kerneldir" --exclude=='.git' | cut -f1)" \
		| pixz -4 > "${sources_pkg_dir}/usr/src/linux-source-${version}-${LINUXFAMILY}.tar.xz"
	cp COPYING "${sources_pkg_dir}/usr/share/doc/linux-source-${version}-${LINUXFAMILY}/LICENSE"

	cat <<-EOF > "${sources_pkg_dir}"/DEBIAN/control
	Package: linux-source-${version}-${BRANCH}-${LINUXFAMILY}
	Version: ${version}-${BRANCH}-${LINUXFAMILY}+${REVISION}
	Architecture: all
	Maintainer: $MAINTAINER <$MAINTAINERMAIL>
	Section: kernel
	Priority: optional
	Depends: binutils, coreutils
	Provides: linux-source, linux-source-${version}-${LINUXFAMILY}
	Recommends: gcc, make
	Description: This package provides the source code for the Linux kernel $version
	EOF

	fakeroot dpkg-deb -b -Z${DEB_COMPRESS} -z0 "${sources_pkg_dir}" "${sources_pkg_dir}.deb"
	rsync --remove-source-files -rq "${sources_pkg_dir}.deb" "${DEB_STORAGE}/"

	te=$(date +%s)
	display_alert "Make the linux-source package" "$(($te - $ts)) sec." "info"
	rm -rf "${tmp_src_dir}"
}

compile_kernel()
{
	if [[ $CLEAN_LEVEL == *make* ]]; then
		display_alert "Cleaning" "$LINUXSOURCEDIR" "info"
		(cd ${LINUXSOURCEDIR}; make ARCH="${ARCHITECTURE}" clean >/dev/null 2>&1)
	fi

	if [[ $USE_OVERLAYFS == yes ]]; then
		local kerneldir
		kerneldir=$(overlayfs_wrapper "wrap" "$LINUXSOURCEDIR" "kernel_${LINUXFAMILY}_${BRANCH}")
	else
		local kerneldir="$LINUXSOURCEDIR"
	fi
	cd "${kerneldir}" || exit

	rm -f localversion

	# read kernel version
	local version 
	version=$(grab_version "$kerneldir")

	# Apply a series of patches if a series file exists
	if test -f "${EXTER}"/patch/kernel/${KERNELPATCHDIR}/series.conf; then
		display_alert "series.conf file visible. Apply"
		series_conf="${SRC}"/patch/kernel/${KERNELPATCHDIR}/series.conf

		# apply_patch_series <target dir> <full path to series file>
		apply_patch_series "${kerneldir}" "$series_conf"
	fi

	# build 3rd party drivers
	# compilation_prepare

	advanced_patch "kernel" "$KERNELPATCHDIR" "$BOARD" "" "$BRANCH" "$LINUXFAMILY-$BRANCH"

	# create patch for manual source changes in debug mode
	[[ $CREATE_PATCHES == yes ]] && userpatch_create "kernel"

	# re-read kernel version after patching
	local version
	version=$(grab_version "$kerneldir")

	display_alert "Compiling $BRANCH kernel" "$version" "info"

	# compare with the architecture of the current Debian node
	# if it matches we use the system compiler
	if $(dpkg-architecture -e "${ARCH}"); then
		display_alert "Native compilation"
	elif [[ $(dpkg --print-architecture) == amd64 ]]; then
		local toolchain
		toolchain=$(find_toolchain "$KERNEL_COMPILER" "$KERNEL_USE_GCC")
		[[ -z $toolchain ]] && exit_with_error "Could not find required toolchain" "${KERNEL_COMPILER}gcc $KERNEL_USE_GCC"
	else
		exit_with_error "Architecture [$ARCH] is not supported"
	fi

	display_alert "Compiler version" "${KERNEL_COMPILER}gcc $(eval env PATH="${toolchain}:${PATH}" "${KERNEL_COMPILER}gcc" -dumpversion)" "info"

	# copy kernel config
	if [[ $KERNEL_KEEP_CONFIG == yes && -f "${DEST}"/config/$LINUXCONFIG.config ]]; then
		display_alert "Using previous kernel config" "${DEST}/config/$LINUXCONFIG.config" "info"
		cp -p "${DEST}/config/${LINUXCONFIG}.config" .config
	else
		display_alert "Using kernel config file" "${EXTER}/config/kernel/$LINUXCONFIG.config" "info"
		cp -p "${EXTER}/config/kernel/${LINUXCONFIG}.config" .config
	fi

	# hack for deb builder. To pack what's missing in headers pack.
	cp "$EXTER"/patch/misc/headers-debian-byteshift.patch /tmp

	if [[ $KERNEL_CONFIGURE != yes ]]; then
		
        # TODO: check if required
        eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${PATH}" \
            'make ARCH=$ARCHITECTURE CROSS_COMPILE="$CCACHE $KERNEL_COMPILER" olddefconfig'

	else
		eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${PATH}" \
			'make $CTHREADS ARCH=$ARCHITECTURE CROSS_COMPILE="$CCACHE $KERNEL_COMPILER" oldconfig'
		eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${PATH}" \
			'make $CTHREADS ARCH=$ARCHITECTURE CROSS_COMPILE="$CCACHE $KERNEL_COMPILER" ${KERNEL_MENUCONFIG:-menuconfig}'

		[[ ${PIPESTATUS[0]} -ne 0 ]] && exit_with_error "Error kernel menuconfig failed"

		# store kernel config in easily reachable place
		display_alert "Exporting new kernel config" "$DEST/config/$LINUXCONFIG.config" "info"
		cp .config "${DEST}/config/${LINUXCONFIG}.config"
		cp .config "${EXTER}/config/kernel/${LINUXCONFIG}.config"
		# export defconfig too if requested
		if [[ $KERNEL_EXPORT_DEFCONFIG == yes ]]; then
			eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${PATH}" \
				'make ARCH=$ARCHITECTURE CROSS_COMPILE="$CCACHE $KERNEL_COMPILER" savedefconfig'
			[[ -f defconfig ]] && cp defconfig "${DEST}/config/${LINUXCONFIG}.defconfig"
		fi
	fi

	# create linux-source package - with already patched sources
	# We will build this package first and clear the memory.
	if [[ $BUILD_KSRC != no ]]; then
		create_linux-source_package
	fi

	echo -e "\n\t== kernel ==\n" >> "${DEST}"/${LOG_SUBPATH}/compilation.log
	eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${PATH}" \
		'make $CTHREADS ARCH=$ARCHITECTURE \
		CROSS_COMPILE="$CCACHE $KERNEL_COMPILER" \
		$SRC_LOADADDR \
		LOCALVERSION="-$LINUXFAMILY" \
		$KERNEL_IMAGE_TYPE ${KERNEL_EXTRA_TARGETS:-modules dtbs} 2>>$DEST/${LOG_SUBPATH}/compilation.log' \
		${PROGRESS_LOG_TO_FILE:+' | tee -a $DEST/${LOG_SUBPATH}/compilation.log'} \
		${OUTPUT_DIALOG:+' | dialog --backtitle "$backtitle" \
		--progressbox "Compiling kernel..." $TTY_Y $TTY_X'} \
		${OUTPUT_VERYSILENT:+' >/dev/null 2>/dev/null'}

	if [[ ${PIPESTATUS[0]} -ne 0 || ! -f arch/$ARCHITECTURE/boot/$KERNEL_IMAGE_TYPE ]]; then
		grep -i error $DEST/${LOG_SUBPATH}/compilation.log
		exit_with_error "Kernel was not built" "@host"
	fi

	# different packaging for 4.3+
	if linux-version compare "${version}" ge 4.3; then
		local kernel_packing="bindeb-pkg"
	else
		local kernel_packing="deb-pkg"
	fi

	display_alert "Creating packages"

	# produce deb packages: image, headers, firmware, dtb
	echo -e "\n\t== deb packages: image, headers, firmware, dtb ==\n" >> "${DEST}"/${LOG_SUBPATH}/compilation.log
	eval CCACHE_BASEDIR="$(pwd)" env PATH="${toolchain}:${PATH}" \
		'make $CTHREADS $kernel_packing \
		KDEB_PKGVERSION=$REVISION \
		KDEB_COMPRESS=${DEB_COMPRESS} \
		BRANCH=$BRANCH \
		LOCALVERSION="-${LINUXFAMILY}" \
		KBUILD_DEBARCH=$ARCH \
		ARCH=$ARCHITECTURE \
		DEBFULLNAME="$MAINTAINER" \
		DEBEMAIL="$MAINTAINERMAIL" \
		CROSS_COMPILE="$CCACHE $KERNEL_COMPILER" 2>>$DEST/${LOG_SUBPATH}/compilation.log' \
		${PROGRESS_LOG_TO_FILE:+' | tee -a $DEST/${LOG_SUBPATH}/compilation.log'} \
		${OUTPUT_DIALOG:+' | dialog --backtitle "$backtitle" --progressbox "Creating kernel packages..." $TTY_Y $TTY_X'} \
		${OUTPUT_VERYSILENT:+' >/dev/null 2>/dev/null'}

	cd .. || exit
	# remove firmare image packages here - easier than patching ~40 packaging scripts at once
	rm -f linux-firmware-image-*.deb

	rsync --remove-source-files -rq ./*.deb "${DEB_STORAGE}/" || exit_with_error "Failed moving kernel DEBs"
}

compile_sunxi_tools()
{
	# Compile and install only if git commit changed
	cd $EXTER/cache/sources/sunxi-tools || exit
	# need to check if /usr/local/bin/sunxi-fexc to detect new Docker containers with old cached sources
    
    display_alert "Compiling" "sunxi-tools" "info"
    make -s clean >/dev/null
    make -s tools >/dev/null
    mkdir -p /usr/local/bin/
    make install-tools >/dev/null 2>&1
}

grab_version()
{
	local ver=()
	ver[0]=$(grep "^VERSION" "${1}"/Makefile | head -1 | awk '{print $(NF)}' | grep -oE '^[[:digit:]]+')
	ver[1]=$(grep "^PATCHLEVEL" "${1}"/Makefile | head -1 | awk '{print $(NF)}' | grep -oE '^[[:digit:]]+')
	ver[2]=$(grep "^SUBLEVEL" "${1}"/Makefile | head -1 | awk '{print $(NF)}' | grep -oE '^[[:digit:]]+')
	ver[3]=$(grep "^EXTRAVERSION" "${1}"/Makefile | head -1 | awk '{print $(NF)}' | grep -oE '^-rc[[:digit:]]+')
	echo "${ver[0]:-0}${ver[1]:+.${ver[1]}}${ver[2]:+.${ver[2]}}${ver[3]}"
}

# find_toolchain <compiler_prefix> <expression>
#
# returns path to toolchain that satisfies <expression>
#
find_toolchain()
{
	[[ "${SKIP_EXTERNAL_TOOLCHAINS}" == "yes" ]] && { echo "/usr/bin"; return; }
	local compiler=$1
	local expression=$2
	local dist=10
	local toolchain=""
	# extract target major.minor version from expression
	local target_ver
	target_ver=$(grep -oE "[[:digit:]]+\.[[:digit:]]" <<< "$expression")
	for dir in "${SRC}"/toolchains/*/; do
		# check if is a toolchain for current $ARCH
		[[ ! -f ${dir}bin/${compiler}gcc ]] && continue
		# get toolchain major.minor version
		local gcc_ver
		gcc_ver=$("${dir}bin/${compiler}gcc" -dumpversion | grep -oE "^[[:digit:]]+\.[[:digit:]]")
		# check if toolchain version satisfies requirement
		awk "BEGIN{exit ! ($gcc_ver $expression)}" >/dev/null || continue
		# check if found version is the closest to target
		# may need different logic here with more than 1 digit minor version numbers
		# numbers: 3.9 > 3.10; versions: 3.9 < 3.10
		# dpkg --compare-versions can be used here if operators are changed
		local d
		d=$(awk '{x = $1 - $2}{printf "%.1f\n", (x > 0) ? x : -x}' <<< "$target_ver $gcc_ver")
		if awk "BEGIN{exit ! ($d < $dist)}" >/dev/null ; then
			dist=$d
			toolchain=${dir}bin
		fi
	done
	echo "$toolchain"
	# logging a stack of used compilers.
	if [[ -f "${DEST}"/${LOG_SUBPATH}/compiler.log ]]; then
		if ! grep -q "$toolchain" "${DEST}"/${LOG_SUBPATH}/compiler.log; then
			echo "$toolchain" >> "${DEST}"/${LOG_SUBPATH}/compiler.log;
		fi
	else
			echo "$toolchain" >> "${DEST}"/${LOG_SUBPATH}/compiler.log;
	fi
}

# advanced_patch <dest> <family> <board> <target> <branch> <description>
#
# parameters:
# <dest>: u-boot, kernel, atf
# <family>: u-boot: u-boot; kernel: sunxi-next, ...
# <board>: orangepipcplus, orangepizero ...
# <target>: optional subdirectory
# <description>: additional description text
#
# priority:
# $EXTER/patch/<dest>/<family>/target_<target>
# $EXTER/patch/<dest>/<family>/board_<board>
# $EXTER/patch/<dest>/<family>/branch_<branch>
# $EXTER/patch/<dest>/<family>
#
advanced_patch()
{
	local dest=$1
	local family=$2
	local board=$3
	local target=$4
	local branch=$5
	local description=$6

	display_alert "Started patching process for" "$dest $description" "info"
	display_alert "Looking for user patches in" "userpatches/$dest/$family" "info"

	local names=()
	local dirs=(
		"$USERPATCHES_PATH/$dest/$family/target_${target}:[\e[33mu\e[0m][\e[34mt\e[0m]"
		"$USERPATCHES_PATH/$dest/$family/board_${board}:[\e[33mu\e[0m][\e[35mb\e[0m]"
		"$USERPATCHES_PATH/$dest/$family/branch_${branch}:[\e[33mu\e[0m][\e[33mb\e[0m]"
		"$USERPATCHES_PATH/$dest/$family:[\e[33mu\e[0m][\e[32mc\e[0m]"
		"$EXTER/patch/$dest/$family/target_${target}:[\e[32ml\e[0m][\e[34mt\e[0m]"
		"$EXTER/patch/$dest/$family/board_${board}:[\e[32ml\e[0m][\e[35mb\e[0m]"
		"$EXTER/patch/$dest/$family/branch_${branch}:[\e[32ml\e[0m][\e[33mb\e[0m]"
		"$EXTER/patch/$dest/$family:[\e[32ml\e[0m][\e[32mc\e[0m]"
		)
	local links=()

	# required for "for" command
	shopt -s nullglob dotglob
	# get patch file names
	for dir in "${dirs[@]}"; do
		for patch in ${dir%%:*}/*.patch; do
			names+=($(basename "${patch}"))
		done
		# add linked patch directories
		if [[ -d ${dir%%:*} ]]; then
			local findlinks
			findlinks=$(find "${dir%%:*}" -maxdepth 1 -type l -print0 2>&1 | xargs -0)
			[[ -n $findlinks ]] && readarray -d '' links < <(find "${findlinks}" -maxdepth 1 -type f -follow -print -iname "*.patch" -print | grep "\.patch$" | sed "s|${dir%%:*}/||g" 2>&1)
		fi
	done
	# merge static and linked
	names=("${names[@]}" "${links[@]}")
	# remove duplicates
	local names_s=($(echo "${names[@]}" | tr ' ' '\n' | LC_ALL=C sort -u | tr '\n' ' '))
	# apply patches
	for name in "${names_s[@]}"; do
		for dir in "${dirs[@]}"; do
			if [[ -f ${dir%%:*}/$name ]]; then
				if [[ -s ${dir%%:*}/$name ]]; then
					process_patch_file "${dir%%:*}/$name" "${dir##*:}"
				else
					display_alert "* ${dir##*:} $name" "skipped"
				fi
				break # next name
			fi
		done
	done
}

# process_patch_file <file> <description>
#
# parameters:
# <file>: path to patch file
# <status>: additional status text
#
process_patch_file()
{
	local patch=$1
	local status=$2

	# detect and remove files which patch will create
	lsdiff -s --strip=1 "${patch}" | grep '^+' | awk '{print $2}' | xargs -I % sh -c 'rm -f %'

	echo "Processing file $patch" >> "${DEST}"/${LOG_SUBPATH}/patching.log
	patch --batch --silent -p1 -N < "${patch}" >> "${DEST}"/${LOG_SUBPATH}/patching.log 2>&1

	if [[ $? -ne 0 ]]; then
		display_alert "* $status $(basename "${patch}")" "failed" "wrn"
		[[ $EXIT_PATCHING_ERROR == yes ]] && exit_with_error "Aborting due to" "EXIT_PATCHING_ERROR"
	else
		display_alert "* $status $(basename "${patch}")" "" "info"
	fi
	echo >> "${DEST}"/${LOG_SUBPATH}/patching.log
}

userpatch_create()
{
	# create commit to start from clean source
	git add .
	git -c user.name='Orange Pi User' -c user.email='user@example.org' commit -q -m "Cleaning working copy"

	local patch="$DEST/patch/$1-$LINUXFAMILY-$BRANCH.patch"

	# apply previous user debug mode created patches
	if [[ -f $patch ]]; then
		display_alert "Applying existing $1 patch" "$patch" "wrn" && patch --batch --silent -p1 -N < "${patch}"
		# read title of a patch in case Git is configured
		if [[ -n $(git config user.email) ]]; then
			COMMIT_MESSAGE=$(cat "${patch}" | grep Subject | sed -n -e '0,/PATCH/s/.*PATCH]//p' | xargs)
			display_alert "Patch name extracted" "$COMMIT_MESSAGE" "wrn"
		fi
	fi

	# prompt to alter source
	display_alert "Make your changes in this directory:" "$(pwd)" "wrn"
	display_alert "Press <Enter> after you are done" "waiting" "wrn"
	read -r </dev/tty
	tput cuu1
	git add .
	# create patch out of changes
	if ! git diff-index --quiet --cached HEAD; then
		# If Git is configured, create proper patch and ask for a name
		if [[ -n $(git config user.email) ]]; then
			display_alert "Add / change patch name" "$COMMIT_MESSAGE" "wrn"
			read -e -p "Patch description: " -i "$COMMIT_MESSAGE" COMMIT_MESSAGE
			[[ -z "$COMMIT_MESSAGE" ]] && COMMIT_MESSAGE="Patching something"
			git commit -s -m "$COMMIT_MESSAGE"
			git format-patch -1 HEAD --stdout --signature="Created with orangepi build tools https://github.com/orangepi-xunlong/build" > "${patch}"
			PATCHFILE=$(git format-patch -1 HEAD)
			rm $PATCHFILE # delete the actual file
			# create a symlink to have a nice name ready
			find $DEST/patch/ -type l -delete # delete any existing
			ln -sf $patch $DEST/patch/$PATCHFILE
		else
			git diff --staged > "${patch}"
		fi
		display_alert "You will find your patch here:" "$patch" "info"
	else
		display_alert "No changes found, skipping patch creation" "" "wrn"
	fi
	git reset --soft HEAD~
	for i in {3..1..1}; do echo -n "$i." && sleep 1; done
}

# overlayfs_wrapper <operation> <workdir> <description>
#
# <operation>: wrap|cleanup
# <workdir>: path to source directory
# <description>: suffix for merged directory to help locating it in /tmp
# return value: new directory
#
# Assumptions/notes:
# - Ubuntu Xenial host
# - /tmp is mounted as tmpfs
# - there is enough space on /tmp
# - UB if running multiple compilation tasks in parallel
# - should not be used with CREATE_PATCHES=yes
#
overlayfs_wrapper()
{
	local operation="$1"
	if [[ $operation == wrap ]]; then
		local srcdir="$2"
		local description="$3"
		mkdir -p /tmp/overlay_components/ /tmp/orangepi_build/
		local tempdir workdir mergeddir
		tempdir=$(mktemp -d --tmpdir="/tmp/overlay_components/")
		workdir=$(mktemp -d --tmpdir="/tmp/overlay_components/")
		mergeddir=$(mktemp -d --suffix="_$description" --tmpdir="/tmp/orangepi_build/")
		mount -t overlay overlay -o lowerdir="$srcdir",upperdir="$tempdir",workdir="$workdir" "$mergeddir"
		# this is executed in a subshell, so use temp files to pass extra data outside
		echo "$tempdir" >> /tmp/.overlayfs_wrapper_cleanup
		echo "$mergeddir" >> /tmp/.overlayfs_wrapper_umount
		echo "$mergeddir" >> /tmp/.overlayfs_wrapper_cleanup
		echo "$mergeddir"
		return
	fi
	if [[ $operation == cleanup ]]; then
		if [[ -f /tmp/.overlayfs_wrapper_umount ]]; then
			for dir in $(</tmp/.overlayfs_wrapper_umount); do
				[[ $dir == /tmp/* ]] && umount -l "$dir" > /dev/null 2>&1
			done
		fi
		if [[ -f /tmp/.overlayfs_wrapper_cleanup ]]; then
			for dir in $(</tmp/.overlayfs_wrapper_cleanup); do
				[[ $dir == /tmp/* ]] && rm -rf "$dir"
			done
		fi
		rm -f /tmp/.overlayfs_wrapper_umount /tmp/.overlayfs_wrapper_cleanup
	fi
}

#!/bin/sh
#
# Oracle VM VirtualBox
# VirtualBox Makeself installation starter script
# for Linux Guest Additions

#
# Copyright (C) 2006-2013 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

# This is a stub installation script to be included in VirtualBox Makeself
# installers which removes any previous installations of the package, unpacks
# the package into the filesystem (by default under /opt) and starts the real
# installation script.
#
PATH=$PATH:/bin:/sbin:/usr/sbin

# Note: These variable names must *not* clash with variables in $CONFIG_DIR/$CONFIG!
PACKAGE="_PACKAGE_"
PACKAGE_NAME="_PACKAGE_NAME_"
UNINSTALL="uninstall.sh"
ROUTINES="routines.sh"
ARCH="_ARCH_"
INSTALLATION_VER="_VERSION_"
INSTALLATION_REV="_SVNREV_"
BUILD_TYPE="_BUILDTYPE_"
USERNAME="_USERNAME_"
UNINSTALL_SCRIPTS="_UNINSTALL_SCRIPTS_"

INSTALLATION_DIR="/opt/$PACKAGE-$INSTALLATION_VER"
CONFIG_DIR="/var/lib/$PACKAGE"
CONFIG="config"
CONFIG_FILES="filelist"
SELF=$1
LOGFILE="/var/log/$PACKAGE.log"

. "./$ROUTINES"

check_root

create_log "$LOGFILE"

## @todo r=andy: Explain options like "force" and "no_setup" -- not self-explanatory
#        to the user.
usage()
{
    info ""
    info "Usage: $SELF install [<installation directory>]"
    info "       [--with-<module>] |"
    info "       uninstall"
    info "       [--force] [--no-setup]"
    info ""
    info "Example:"
    info "$SELF install"
    exit 1
}

# Create a symlink in the filesystem and add it to the list of package files
add_symlink()
{
    self=add_symlink
    ## Parameters:
    # The file the link should point to
    target="$1"
    # The name of the actual symlink file.  Must be an absolute path to a
    # non-existing file in an existing directory.
    link="$2"
    link_dir="`dirname "$link"`"
    test -n "$target" ||
        { echo 1>&2 "$self: no target specified"; return 1; }
    test -d "$link_dir" ||
        { echo 1>&2 "$self: link directory $link_dir does not exist"; return 1; }
    test ! -e "$link" ||
        { echo 1>&2 "$self: link file "$link" already exists"; return 1; }
    expr "$link" : "/.*" > /dev/null ||
        { echo 1>&2 "$self: link file name is not absolute"; return 1; }
    rm -f "$link"
    ln -s "$target" "$link"
    echo "$link" >> "$CONFIG_DIR/$CONFIG_FILES"
}

# Create symbolic links targeting all files in a directory in another
# directory in the filesystem
link_into_fs()
{
    ## Parameters:
    # Directory containing the link target files
    target_branch="$1"
    # Directory to create the link files in
    directory="$2"
    for i in "$INSTALLATION_DIR/$target_branch"/*; do
        test -e "$i" &&
            add_symlink "$i" "$directory/`basename $i`"
    done
}

# Look for broken installations or installations without a known uninstaller
# and try to clean them up, asking the user first.
def_uninstall()
{
    ## Parameters:
    # Whether to force cleanup without asking the user
    force="$1"

    . ./deffiles
    found=0
    for i in $DEFAULT_FILE_NAMES; do
        test "$found" = 0 -a -e "$i" && found=1
    done
    test "$found" = 0 &&
        for i in $DEFAULT_VERSIONED_FILE_NAMES-*; do
            test "$found" = 0 -a -e "$i" && found=1
        done
    test "$found" = 0 && return 0
    if ! test "$1" = "force" ; then
        cat 1>&2 << EOF
You appear to have a version of the _PACKAGE_ software
on your system which was installed from a different source or using a
different type of installer.  If you installed it from a package from your
Linux distribution or if it is a default part of the system then we strongly
recommend that you cancel this installation and remove it properly before
installing this version.  If this is simply an older or a damaged
installation you may safely proceed.

Do you wish to continue anyway? [yes or no]
EOF
        read reply dummy
        if ! expr "$reply" : [yY] > /dev/null &&
            ! expr "$reply" : [yY][eE][sS] > /dev/null
        then
            info
            info "Cancelling installation."
            return 1
        fi
    fi
    # Stop what we can in the way of services and remove them from the
    # system
    for i in $UNINSTALL_SCRIPTS; do
        stop_init_script "$i"
        cleanup_init "$i" 1>&2 2>> "$LOGFILE"
        test -x "./$i" && "./$i" cleanup 1>&2 2>> "$LOGFILE"
        remove_init_script "$i"
    done

    # Get rid of any remaining files
    for i in $DEFAULT_FILE_NAMES; do
        rm -f "$i" 2> /dev/null
    done
    for i in $DEFAULT_VERSIONED_FILE_NAMES; do
        rm -f "$i-"* 2> /dev/null
    done
    rm -f "/usr/lib/$PACKAGE" "/usr/lib64/$PACKAGE" "/usr/share/$PACKAGE"

    # And any packages left under /opt
    for i in "/opt/$PACKAGE-"*; do
        test -d "$i" && rm -rf "$i"
    done
    return 0
}

info "$PACKAGE_NAME installer"

check_bzip2

# Check architecture
cpu=`uname -m`;
case "$cpu" in
  i[3456789]86|x86)
    cpu="x86"
    lib_path="/usr/lib"
    ;;
  x86_64|amd64)
    cpu="amd64"
    if test -d "/usr/lib64"; then
      lib_path="/usr/lib64"
    else
      lib_path="/usr/lib"
    fi
    ;;
  *)
    cpu="unknown"
esac
ARCH_PACKAGE="$PACKAGE-$cpu.tar.bz2"
if [ ! -r "$ARCH_PACKAGE" ]; then
  info "Detected unsupported $cpu machine type."
  exit 1
fi

# Sensible default actions
ACTION="install"
DO_SETUP="true"
NO_CLEANUP=""
FORCE_UPGRADE=""

while [ $# -ge 2 ];
do
    ARG=$2
    shift

    if [ -z "$MY_END_OF_OPTIONS" ]; then
        case "$ARG" in

            install)
                ACTION="install"
                ;;

            uninstall)
                ACTION="uninstall"
                ;;

            ## @todo Add per-module options handling, e.g. --lightdm-greeter-dir
             #       or --lightdm-config

            ## @todo Add listing all available modules (+ their options, e.g.
             #       with callback mod_mymod_show_options?)

            --with-*)
                MODULE_CUR=`expr "$ARG" : '--with-\(.*\)'`
                # Check if corresponding module in installer/module-$1 exists.
                # Note: Module names may not contain spaces or other funny things.
                if [ ! -f "./installer/module-${MODULE_CUR}" ]; then
                    info "Error: Module \"${MODULE_CUR}\" does not exist."
                    usage
                fi
                # Give the module the chance of doing initialization work / checks.
                . "./installer/module-${MODULE_CUR}"
                mod_${MODULE_CUR}_init
                if test $? -ne 0; then
                    echo 1>&2 "Module '${MODULE_CUR}' failed to initialize"
                    if ! test "$FORCE_UPGRADE" = "force"; then
                        return 1
                    fi
                    # Continue initialization.
                fi
                # Add module to the list of modules to handle later.
                if test -z "${INSTALLATION_MODULES_LIST}"; then
                    INSTALLATION_MODULES_LIST="${MODULE_CUR}"
                else
                    INSTALLATION_MODULES_LIST="${INSTALLATION_MODULES_LIST} ${MODULE_CUR}"
                fi
                shift
                ;;

            --force|force) # Keep "force" for backwards compatibility.
                FORCE_UPGRADE="force"
                ;;

            --no-setup|no_setup) # Keep "no_setup" for backwards compatibility.
                DO_SETUP=""
                ;;

            --no-cleanup|no_cleanup) # Keep "no_cleanup" for backwards compatibility.
                # Do not do cleanup of old modules when removing them.  For
                # testing purposes only.
                DO_SETUP=""
                NO_CLEANUP="no_cleanup"
                ;;

            --)
                MY_END_OF_OPTIONS="1"
                ;;

            *)
                if [ "`echo $1|cut -c1`" != "/" ]; then
                    info "Please specify an absolute path"
                    usage
                fi
                INSTALLATION_DIR="$1"
                shift
                ;;
        esac
    fi
done

# uninstall any previous installation
INSTALL_DIR=""
uninstalled=0
test -r "$CONFIG_DIR/$CONFIG" && . "$CONFIG_DIR/$CONFIG"
if test -n "$INSTALL_DIR" -a -x "$INSTALL_DIR/$UNINSTALLER"; then
  "$INSTALL_DIR/$UNINSTALLER" $NO_CLEANUP 1>&2 ||
    abort "Failed to remove existing installation.  Aborting..."
  uninstalled=1
fi
test "$uninstalled" = 0 && def_uninstall "$FORCE_UPGRADE" && uninstalled=1
test "$uninstalled" = 0 && exit 1
rm -f "$CONFIG_DIR/$CONFIG"
rm -f "$CONFIG_DIR/$CONFIG_FILES"
rmdir "$CONFIG_DIR" 2>/dev/null
test "$ACTION" = "install" || exit 0

# Set installer modules directory
INSTALLATION_MODULES_DIR="$INSTALLATION_DIR/installer/"

# install and load installer modules
if [ -d installer ]; then
  info "Copying additional installer modules ..."
  mkdir -p -m 755 "$INSTALLATION_MODULES_DIR"
  for CUR_FILE in `ls installer/*`; do
      install -p -m 755 "$CUR_FILE" "$INSTALLATION_MODULES_DIR"
      if [ $? -ne 0 ]; then
          info "Error: Failed to copy installer module \"$CUR_FILE\""
          if ! test "$FORCE_UPGRADE" = "force"; then
              exit 1
          fi        
      fi
  done
fi

# install the new version
mkdir -p -m 755 "$CONFIG_DIR"
test ! -d "$INSTALLATION_DIR" && REMOVE_INSTALLATION_DIR=1
mkdir -p -m 755 "$INSTALLATION_DIR"
# Create a list of the files in the archive, skipping any directories which
# already exist in the filesystem.
bzip2 -d -c "$ARCH_PACKAGE" | tar -tf - |
  while read name; do
    fullname="$INSTALLATION_DIR/$name"
    case "$fullname" in
      */)
        test ! -d "$fullname" &&
          echo "$fullname" >> "$CONFIG_DIR/$CONFIG_FILES"
        ;;
      *)
        echo "$fullname" >> "$CONFIG_DIR/$CONFIG_FILES"
        ;;
    esac
  done
bzip2 -d -c "$ARCH_PACKAGE" | tar -xf - -C "$INSTALLATION_DIR" || exit 1

# Set symlinks into /usr and /sbin
link_into_fs "bin" "/usr/bin"
link_into_fs "sbin" "/usr/sbin"
link_into_fs "lib" "$lib_path"
link_into_fs "share" "/usr/share"
link_into_fs "src" "/usr/src"

if [ -d "$INSTALLATION_MODULES_DIR" ]; then
  info "Installing additional modules ..."
  for CUR_MODULE in `find "$INSTALLATION_MODULES_DIR" 2>/dev/null`
      do
          echo "$CUR_MODULE" >> "$CONFIG_DIR/$CONFIG_FILES"
      done
fi

for CUR_MODULE in ${INSTALLATION_MODULES_LIST}
do
    mod_${CUR_MODULE}_install
    if [ $? -ne 0 ]; then
        info "Error: Failed to install module \"$CUR_MODULE\""
        if ! test "$FORCE_UPGRADE" = "force"; then
            exit 1
        fi        
    fi
done

# Remember our installation configuration before we call any init scripts
cat > "$CONFIG_DIR/$CONFIG" << EOF
# $PACKAGE installation record.
# Package installation directory
INSTALL_DIR='$INSTALLATION_DIR'
# Additional installation modules
INSTALL_MODULES_DIR='$INSTALLATION_MODULES_DIR'
INSTALL_MODULES_LIST='$INSTALLATION_MODULES_LIST'
# Package uninstaller.  If you repackage this software, please make sure
# that this prints a message and returns an error so that the default
# uninstaller does not attempt to delete the files installed by your
# package.
UNINSTALLER='$UNINSTALL'
# Package version
INSTALL_VER='$INSTALLATION_VER'
INSTALL_REV='$INSTALLATION_REV'
# Build type and user name for logging purposes
BUILD_TYPE='$BUILD_TYPE'
USERNAME='$USERNAME'
EOF

# Give the modules the chance to write their stuff
# to the installation config as well.
if [ -n "${INSTALLATION_MODULES_LIST}" ]; then
  info "Saving modules configuration ..."
  for CUR_MODULE in ${INSTALLATION_MODULES_LIST}
  do
    echo "`mod_${CUR_MODULE}_config_save`" >> "$CONFIG_DIR/$CONFIG"
  done
fi

# Install, set up and start init scripts
for i in "$INSTALLATION_DIR/init/"*; do
  if test -r "$i"; then
    install_init_script "$i" "`basename "$i"`"
    test -n "$DO_SETUP" && setup_init_script "`basename "$i"`" 1>&2
    start_init_script "`basename "$i"`"
  fi
done

cp $ROUTINES $INSTALLATION_DIR
echo $INSTALLATION_DIR/$ROUTINES >> "$CONFIG_DIR/$CONFIG_FILES"
cat > $INSTALLATION_DIR/$UNINSTALL << EOF
#!/bin/sh
# Auto-generated uninstallation file

PATH=\$PATH:/bin:/sbin:/usr/sbin
LOGFILE="/var/log/$PACKAGE-uninstall.log"

# Read routines.sh
if ! test -r "$INSTALLATION_DIR/$ROUTINES"; then
    echo 1>&2 "Required file $ROUTINES not found.  Aborting..."
    return 1
fi
. "$INSTALLATION_DIR/$ROUTINES"

# We need to be run as root
check_root

create_log "\$LOGFILE"

echo 1>&2 "Removing installed version $INSTALLATION_VER of $PACKAGE_NAME..."

NO_CLEANUP=""
if test "\$1" = "no_cleanup"; then
    shift
    NO_CLEANUP="no_cleanup"
fi

test -r "$CONFIG_DIR/$CONFIG_FILES" || abort "Required file $CONFIG_FILES not found.  Aborting..."

# Stop and clean up all services
for i in "$INSTALLATION_DIR/init/"*; do
    if test -r "\$i"; then
        stop_init_script "\`basename "\$i"\`"
        test -z "\$NO_CLEANUP" && cleanup_init "\`basename "\$i"\`" 2>> "\$LOGFILE"
        remove_init_script "\`basename "\$i"\`"
    fi
done

# Load all modules
# Important: This needs to be done before loading the configuration
#            value below to not override values which are set to a default
#            value in the modules itself.
for CUR_MODULE in `find "$INSTALLATION_MODULES_DIR" -name "module-*" 2>/dev/null`
    do
        . "\$CUR_MODULE"
    done

# Load configuration values
test -r "$CONFIG_DIR/$CONFIG" && . "$CONFIG_DIR/$CONFIG"

# Call uninstallation initialization of all modules
for CUR_MODULE in "$INSTALLATION_MODULES_LIST"
    do
        if test -z "\$CUR_MODULE"; then
            continue
        fi
        mod_\${CUR_MODULE}_pre_uninstall
        if [ $? -ne 0 ]; then
            echo 1>&2 "Module \"\$CUR_MODULE\" failed to initialize uninstallation"
            # Continue initialization.
        fi
    done

# Call uninstallation of all modules
for CUR_MODULE in "$INSTALLATION_MODULES_LIST"
    do
        if test -z "\$CUR_MODULE"; then
            continue
        fi
        mod_\${CUR_MODULE}_uninstall
        if [ $? -ne 0 ]; then
            echo 1>&2 "Module \"\$CUR_MODULE\" failed to uninstall"
            # Continue uninstallation.
        fi
    done

# And remove all files and empty installation directories
# Remove any non-directory entries
cat "$CONFIG_DIR/$CONFIG_FILES" | xargs rm 2>/dev/null
# Remove any empty (of files) directories in the file list
cat "$CONFIG_DIR/$CONFIG_FILES" |
    while read file; do
        case "\$file" in
            */)
            test -d "\$file" &&
                find "\$file" -depth -type d -exec rmdir '{}' ';' 2>/dev/null
            ;;
        esac
    done

# Remove configuration files
rm "$CONFIG_DIR/$CONFIG_FILES" 2>/dev/null
rm "$CONFIG_DIR/$CONFIG" 2>/dev/null
rmdir "$CONFIG_DIR" 2>/dev/null
exit 0
EOF
chmod 0755 $INSTALLATION_DIR/$UNINSTALL
echo $INSTALLATION_DIR/$UNINSTALL >> "$CONFIG_DIR/$CONFIG_FILES"
test -n "$REMOVE_INSTALLATION_DIR" &&
  echo "$INSTALLATION_DIR/" >> "$CONFIG_DIR/$CONFIG_FILES"


#!/bin/sh
#
# Build version string and increment Services build number.
#

# Grab the actual version from the control file
CTRL="version.log"
if [ -f $CTRL ] ; then
	. $CTRL
else
	echo "Error: Unable to find control file: $CTRL"
	exit 0
fi

if [ $VERSION_BUILD -gt 0 ]; then
	VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_BUILD}${VERSION_EXTRA}"
else
	VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}${VERSION_EXTRA}"
fi

if [ -f version.h ] ; then
	BUILD=`fgrep '#define BUILD' version.h | sed 's/^#define BUILD.*"\([0-9]*\)".*$/\1/'`
	BUILD=`expr $BUILD + 1 2>/dev/null`
else
	BUILD=1
fi
if [ ! "$BUILD" ] ; then
	BUILD=1
fi
cat >version.h <<EOF
/* Version information for Services.
 *
 * (C) 2003 Anope Team
 * Contact us at info@anope.org
 *
 * Please read COPYING and CREDITS for furhter details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church. 
 * 
 * This file is auto-generated by version.sh
 *
 */

#define VERSION_MAJOR	"$VERSION_MAJOR"
#define VERSION_MINOR	"$VERSION_MINOR"
#define VERSION_PATCH	"$VERSION_PATCH"
#define VERSION_BUILD	"$VERSION_BUILD"
#define VERSION_EXTRA	"$VERSION_EXTRA"

#define BUILD	"$BUILD"

const char version_number[] = "$VERSION";
const char version_build[] =
	"build #" BUILD ", compiled " __DATE__ " " __TIME__;

const char version_protocol[] =
#if defined(IRC_ULTIMATE3)
 	"UltimateIRCd 3.0.0.a26+"
#define VER_IRCD "UltimateIRCd 3.0.* -"
#elif defined(IRC_VIAGRA)
	"ViagraIRCd 1.3.x"
# define VER_IRCD "ViagraIRCd 1.3.* -"
#elif defined(IRC_BAHAMUT)
	"Bahamut 1.4.27+"
# define VER_IRCD "BahamutIRCd 1.4.* -"
#elif defined(IRC_ULTIMATE)
	"UltimateIRCd 2.8.2+"
# define VER_IRCD "UltimateIRCd 2.8.* -"
#elif defined(IRC_UNREAL)
	"UnrealIRCd 3.1.1+"
# define VER_IRCD "UnrealIRCd -"
#elif defined(IRC_DREAMFORGE)
	"DreamForge 4.6.7"
# define VER_IRCD "DreamForgeIRCd 4.6.7 -"
#elif defined(IRC_HYBRID)
	"Hybrid IRCd 7.0"
# define VER_IRCD "HybridIRCd 7.* -"
#elif defined(IRC_PTLINK)
       "PTlink 6.14.5+"
# define VER_IRCD "PTlinkIRCd 6.14.* -"
#else
	"unknown"
# define VER_IRCD
#endif
	;

#ifdef DEBUG_COMMANDS
# define VER_DEBUG "D"
#else
# define VER_DEBUG
#endif

#if defined(USE_ENCRYPTION)
# if defined(ENCRYPT_MD5)
#  define VER_ENCRYPTION "E"
# else
#  define VER_ENCRYPTION "E"
# endif
#else
# define VER_ENCRYPTION
#endif

#ifdef USE_THREADS
# define VER_THREAD "T"
#else
# define VER_THREAD
#endif

#if defined(LINUX20)
# define VER_OS "l"
#elif defined(LINUX22)
# define VER_OS "L"
#elif defined(JAGUAR)
# define VER_OS "J"
#elif defined(MACOSX)
# define VER_OS "X"
#else
# define VER_OS
#endif

#if defined(HAVE_GETHOSTBYNAME_R6)
# define VER_GHBNR "6"
#elif defined(HAVE_GETHOSTBYNAME_R5)
# define VER_GHBNR "5"
#elif defined(HAVE_GETHOSTBYNAME_R3)
# define VER_GHBNR "3"
#else
# define VER_GHBNR
#endif

#if defined(USE_MYSQL)
# define VER_MYSQL "Q"
#else
# define VER_MYSQL
#endif

#if defined(USE_MODULES)
# define VER_MODULE "M"
#else
# define VER_MODULE
#endif

const char version_flags[] = VER_IRCD VER_DEBUG VER_ENCRYPTION VER_THREAD VER_OS VER_GHBNR VER_MYSQL VER_MODULE;

EOF

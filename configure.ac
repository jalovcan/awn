AC_INIT([avant-window-navigator],
        [0.4.2],
        [https://bugs.launchpad.net/awn])

EXTRA_VERSION=
AC_ARG_ENABLE([extra-version],
              AS_HELP_STRING([--enable-extra-version],
                             [Adds a custom string to the end of the version. Useful for packagers.]),
              [EXTRA_VERSION="$enableval"])
AC_SUBST(EXTRA_VERSION)
AC_DEFINE_UNQUOTED([EXTRA_VERSION], ["$EXTRA_VERSION"],
                   [Extra version metadata provided by ./configure])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([.])
AC_CONFIG_HEADERS([config.h])

m4_define(pycairo_required_version,      1.0.2) dnl or 1.1.7
m4_define(pygtk_required_version,        2.12.0)

AM_INIT_AUTOMAKE([1.8 -Wno-portability tar-ustar])
AM_MAINTAINER_MODE

AC_SUBST([ACLOCAL_AMFLAGS], ["\${ACLOCAL_FLAGS}"])

AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CXX
AC_STDC_HEADERS
AM_PROG_LIBTOOL

dnl ==============================================
dnl GTK-Doc
dnl ==============================================

GTK_DOC_CHECK(1.4)

dnl ==============================================
dnl Python support
dnl ==============================================

AM_PATH_PYTHON(2.5)
AC_PYTHON_DEVEL

dnl ==============================================
dnl PyGTK
dnl ==============================================

PKG_CHECK_MODULES(PYGTK, gtk+-2.0 pycairo >= pycairo_required_version pygtk-2.0 >= pygtk_required_version)
AC_SUBST(PYGTK_CFLAGS)
AC_PATH_PROGS(PYG_CODEGEN, [pygobject-codegen-2.0 pygtk-codegen-2.0], no)
if test "x$PY_CODEGEN" = "xno"; then
  AC_MSG_ERROR([could not find the PyGObject code generator script])
fi

AC_MSG_CHECKING(for pygtk defs)
PYGTK_DEFSDIR=`$PKG_CONFIG --variable=defsdir pygtk-2.0`
AC_SUBST(PYGTK_DEFSDIR)
AC_MSG_RESULT($PYGTK_DEFSDIR)

dnl ==============================================
dnl Vala support
dnl ==============================================

VALA_PROG_VALAC(0.9.1)
if test "$USE_MAINTAINER_MODE" = "yes"; then
    AC_PATH_PROG(VALA_GEN_INTROSPECT, vala-gen-introspect, vala-gen-introspect, [])
    AC_PATH_PROG(VAPIGEN, vapigen, vapigen, [])
fi

dnl ==============================================
dnl Check for Python modules
dnl ==============================================
AC_ARG_ENABLE(pymod_checks,
              AC_HELP_STRING([--disable-pymod-checks], [Disable checks for Python modules]),
              enable_pymod_checks=$enableval,
              enable_pymod_checks=yes)

if test x${enable_pymod_checks} = xyes; then
  AM_CHECK_PYMOD(xdg,,,AC_MSG_ERROR([The xdg python module is required (pyxdg or python-xdg)]))
fi

dnl ==============================================
dnl CFLAGS
dnl ==============================================

STANDARD_CPPFLAGS="-std=c99 -D_GNU_SOURCE"
if test "$USE_MAINTAINER_MODE" = "yes"; then
  AS_COMPILER_FLAGS([WARNING_FLAGS], ["-Wall -Werror=all -Werror=format -Werror=implicit-function-declaration -Werror=implicit-int -Werror=missing-braces -Werror=parentheses -Werror=return-type -Werror=strict-aliasing -Werror=switch -Wuninitialized -Werror=unused-label -Werror=unused-value -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Werror=missing-declarations -Wredundant-decls -Wmissing-noreturn -Wpointer-arith -Wcast-align -Wwrite-strings -Werror=inline -Werror=format-nonliteral -Wformat-nonliteral -Werror=format-security -Wformat-security -Winit-self -Werror=missing-include-dirs -Werror=undef -Werror=aggregate-return -Wmissing-format-attribute -Werror=nested-externs -fno-strict-aliasing -fmessage-length=0 -Wp,-D_FORTIFY_SOURCE=2"])
  DISABLE_DEPRECATED_FLAGS="-DG_DISABLE_SINGLE_INCLUDES -DGDK_PIXBUF_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_DEPRECATED -DGTK_DISABLE_SINGLE_INCLUDES"
else
  AS_COMPILER_FLAGS([WARNING_FLAGS], ["-Wall"])
fi
AC_SUBST(STANDARD_CPPFLAGS)
AC_SUBST(WARNING_FLAGS)
AC_SUBST(DISABLE_DEPRECATED_FLAGS)

dnl ==============================================
dnl translation support
dnl ==============================================

GETTEXT_PACKAGE=avant-window-navigator
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])
ALL_LINGUAS="af ar ast az be bg bn bs ca crh csb cs cy da de dv el en_AU en_CA en_GB eo es et eu fa fi fr ga gl gu he hi hr hu id is it ja jv kab ka ko ku lt lv mk ml mn mr ms nb nds ne nl nn oc pl pt_BR pt ro ru si sk sl sq sr sv ta te th tr uk vi zh_CN zh_HK zh_TW"

IT_PROG_INTLTOOL([0.34])
AM_GLIB_GNU_GETTEXT

INTLTOOL_SCHEMA_INI_RULE='%.schema-ini:	%.schema-ini.in	$(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*.po) ; LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< [$]@'
AC_SUBST(INTLTOOL_SCHEMA_INI_RULE)

dnl ==============================================
dnl Check that we meet the dependencies
dnl ==============================================

MIN_GLIB_VERSION=2.16.0
MIN_GLIBMM_VERSION=2.16.0
MIN_GTK_VERSION=2.12.0
MIN_GTKMM_VERSION=2.12.0
MIN_WNCK_VERSION=2.22
MIN_LDA_VERSION=0.3.91

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal, glib-genmarshal)

LIBRARY_MODULES="glib-2.0 >= $MIN_GLIB_VERSION glibmm-2.4 >= $MIN_GLIBMM_VERSION gthread-2.0 gobject-2.0 desktop-agnostic >= $MIN_LDA_VERSION gtk+-2.0 >= $MIN_GTK_VERSION gtkmm-2.4 >= $MIN_GTKMM_VERSION gdk-2.0 >= $MIN_GTK_VERSION dbus-glib-1"
DOCK_MODULES="x11 xproto xcomposite xrender xext"
TASKMANAGER_MODULES="libwnck-1.0 >= $MIN_WNCK_VERSION x11 libgtop-2.0 xext"
AC_SUBST(LIBRARY_MODULES)

PKG_CHECK_EXISTS([dbus-glib-1 >= 0.80], [AC_DEFINE(HAVE_DBUS_GLIB_080, 1, [Have dbus-glib which supports GetAll method properly])])

PKG_CHECK_MODULES(AWN, [$LIBRARY_MODULES])
PKG_CHECK_MODULES(DOCK, [$DOCK_MODULES])
PKG_CHECK_MODULES(TASKMANAGER, [$LIBRARY_MODULES $TASKMANAGER_MODULES])

LDA_BINDIR="`$PKG_CONFIG --variable=exec_prefix desktop-agnostic`/bin"
AC_SUBST(LDA_BINDIR)

LDA_DEFSDIR=`$PKG_CONFIG --variable=defsdir desktop-agnostic`
AC_SUBST(LDA_DEFSDIR)

LDA_VAPIDIR="`$PKG_CONFIG --variable=vapidir desktop-agnostic`"
AC_SUBST(LDA_VAPIDIR)

AC_CHECK_LIB(m, lround)

dnl ==============================================
dnl DBus
dnl ==============================================

DBUS_GLIB_BIN="`$PKG_CONFIG --variable=exec_prefix dbus-glib-1`/bin"
AC_SUBST(DBUS_GLIB_BIN)

dnl ==============================================
dnl GConf
dnl ==============================================
AM_GCONF_SOURCE_2
if test "$enable_schemas_install" != "no"; then
    AC_PATH_PROG(GCONFTOOL, gconftool-2, no)
    if test x"$GCONFTOOL" = xno; then
        AC_MSG_ERROR([gconftool-2 executable not found in your path - should be installed with GConf])
    fi
fi

dnl ==============================================
dnl Miscellaneous
dnl ==============================================

AS_AC_EXPAND([PKGDATADIR], [$datadir/$PACKAGE_NAME])
AS_AC_EXPAND([DATADIR], [$datadir])
AS_AC_EXPAND([LIBDIR], [$libdir])
AS_AC_EXPAND([LIBAPPLETDIR], [$libdir/awn/applets])
AS_AC_EXPAND([APPLETSDIR], [$PKGDATADIR/applets])
AS_AC_EXPAND([localedir_intermediate], [$localedir])
AS_AC_EXPAND([LOCALEDIR], [$localedir_intermediate])

AC_DEFINE_UNQUOTED(APPLETDATADIR, "$APPLETSDIR",
                   [The base directory for applet data])

AC_DEFINE_UNQUOTED(LOCALEDIR, "$LOCALEDIR", [Locale directory])
AC_SUBST(LOCALEDIR)

SHAVE_INIT([shave], [enable])

dnl ==============================================
dnl Output
dnl ==============================================

dnl Please keep alphabetized (case-sensitive)
AC_OUTPUT([
Makefile
applet-activation/Makefile
applets/expander/Makefile
applets/expander/expander.desktop.in
applets/quick-prefs/Makefile
applets/quick-prefs/quick-prefs.desktop.in
applets/separator/Makefile
applets/separator/separator.desktop.in
applets/simple-launcher/Makefile
applets/simple-launcher/simple-launcher.desktop.in
applets/taskmanager/Makefile
applets/taskmanager/taskmanager.desktop.in
awn.pc
awn-settings/Makefile
awn-settings/awn_defs.py
awn-settings/awn_settings_launcher.py
bindings/python/Makefile
bindings/python/__init__.py
bindings/vala/Makefile
data/Makefile
data/avant-window-navigator.schema-ini.in
doc/Makefile
doc/reference/Makefile
libawn/Makefile
po/Makefile.in
shave/shave
shave/shave-libtool
src/Makefile
tests/Makefile
themes/clearlooks-dark.awn-theme
themes/dark.awn-theme
themes/dust.awn-theme
themes/light.awn-theme
themes/smoked.awn-theme
themes/Makefile
])

dnl ========================================================================

echo ""
echo "             Avant Window Navigator $VERSION"
echo "             ============================"
echo ""
echo "                   prefix:   ${prefix}"
echo ""
echo "            Documentation:   ${enable_gtk_doc}"
echo ""

include ../utils/Makefrag

export uname := Linux

# zlib.net often returns HTTP 200 HTML (WAF/landing page) to CI/cloud IPs;
# GitHub release assets are the same official 1.3.2 tarball.
SOURCE_URL = https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
SOURCE_URL_FALLBACKS = https://zlib.net/fossils/zlib-1.3.2.tar.gz
SOURCE_FILE = zlib-1.3.2.tar.gz
SOURCE_DIR = zlib-1.3.2
CONFIG_ARGS = --prefix=$(PREFIX) --enable-shared

$(eval $(call define_autotools_package))

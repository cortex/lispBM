{
  build32 ? false,
  supportedSystems,

  self,
  nix-filter,

  multiStdenv,
  readline,
  libpng,
  gcc_multi,
  SDL2,
  SDL2_image,
  freetype,
  pkg-config,
}:

let
  makeTarget = if build32 then "repl" else "repl";
  name = if build32 then "lbm" else "lbm64";
  features = if build32 then "" else "64";
in
multiStdenv.mkDerivation {
  pname = name;
  # IDK what pattern should be used to get ahold of the version number...
  version = self.shortRev or self.rev or self.dirtyShortRev or "unknown";

  meta = {
    description = "LispBM repl";
    platforms = supportedSystems;
  };

  src = nix-filter {
    root = self;
    include = with nix-filter.lib; [
      "src"
      (and "repl" (or_ isDirectory (or_ (matchExt "c") (matchExt "h"))))
      "include"
      "platform"
      "repl/Makefile"
      "lispbm.mk"
    ];
  };

  buildPhase = ''
    cd repl/
    make ${makeTarget} FEATURES="${features} sdl freetype"
  '';
  installPhase = ''
    mkdir -p $out/bin

    cp repl $out/bin/${name}
  '';

  buildInputs = [
    readline
    libpng
    SDL2
    SDL2_image
    freetype
  ];

  nativeBuildInputs = [
    gcc_multi
    SDL2
    SDL2_image
    pkg-config
  ];
}

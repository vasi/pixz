{
  pkgs ? import <nixpkgs> { },
}:
pkgs.mkShell {
  buildInputs = with pkgs.buildPackages; [
    libarchive
    xz
  ];
  nativeBuildInputs = with pkgs; [
    autoconf
    automake
    pkg-config
    gnumake
    asciidoc
    cppcheck
    which # check for cppcheck
  ];
}

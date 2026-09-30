let
  nixpkgs = builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/eaad089433ca2bb662274377d33df3d0e51ef28b.tar.gz";
    sha256 = "sha256-H6GwaZzZf+4npqv0tph94w9tZddSjFjmQrVsW0z78uk=";
  };
  pkgs = import nixpkgs { };
  # Only the CLI's Python dependencies are needed for compilation, not flashers.
  qmk = pkgs.qmk.overrideAttrs (_: {
    propagatedBuildInputs = with pkgs.python3.pkgs; [
      dotty-dict hid hjson jsonschema milc pygments pyserial pyusb pillow
    ];
  });
in
pkgs.mkShellNoCC {
  packages = with pkgs; [
    qmk
    gcc-arm-embedded
    gnumake
    git
    bash
    coreutils
    diffutils
    findutils
    gnugrep
    gnused
    gawk
    python3
  ];
}

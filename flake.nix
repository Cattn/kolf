{
  description = "Kolf with online multiplayer";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/1d060086d60cb76a40cf37109f979d88a5bd6396";

  outputs = { self, nixpkgs }:
    let
      forAllSystems = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];
    in {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          kolf = pkgs.kdePackages.kolf.overrideAttrs (old: {
            version = "cattn-${self.shortRev or "dev"}";
            src = self;
            buildInputs = (old.buildInputs or [ ]) ++ [
              pkgs.kdePackages.qtwebsockets
              pkgs.kdePackages.kjobwidgets
            ];
            cmakeFlags = (old.cmakeFlags or [ ]) ++ [ "-DKOLF_TEST_AUTOMATION=OFF" ];
          });
        in {
          inherit kolf;
          default = kolf;
        });
    };
}

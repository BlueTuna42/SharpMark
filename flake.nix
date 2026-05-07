{
  description = "SharpMark: Fast Image Sharpness Analyzer and Culling Tool";

  inputs = {
    # Using the unstable branch for the latest GTK and LibRaw versions
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # Supported systems for the desktop application
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      
      # Helper function to generate an attrset for all systems
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      
      # Nixpkgs instantiated for each system
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });
    in
    {
      # The main package (buildable with 'nix build')
      packages = forAllSystems (system:
        let
          pkgs = nixpkgsFor.${system};
        in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "sharpmark";
            version = "1.0.0";
            
            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
              wrapGAppsHook3 # Required for GTK apps to handle icons/schemas
            ];

            buildInputs = with pkgs; [
              gtk3
              libraw
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
            ];

            meta = with pkgs.lib; {
              description = "Fast Image Sharpness Analyzer and Culling Tool";
              license = licenses.asl20;
              platforms = platforms.linux;
              mainProgram = "SharpMark";
            };
          };
        });

      # Apps output allows running the app directly with 'nix run'
      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/SharpMark";
        };
      });

      # Development shell (active with 'nix develop')
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgsFor.${system};
        in
        {
          default = pkgs.mkShell {
            # Automatically include all inputs from the default package
            inputsFrom = [ self.packages.${system}.default ];
            
            # Additional tools for local development
            nativeBuildInputs = with pkgs; [
              gdb
            ];
          };
        });

      # Formatter for 'nix fmt'
      formatter = forAllSystems (system: nixpkgsFor.${system}.nixpkgs-fmt);
    };
}
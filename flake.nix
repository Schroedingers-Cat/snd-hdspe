{
	description = "Audio driver for the RME HDSPe range of cards";
	inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

	outputs = { self, nixpkgs }: 
	let
		system = "x86_64-linux";
		pkgs = nixpkgs.legacyPackages.${system};
		
		snd-hdspe = {
			lib,
			stdenv,
			kernel,
			python3,
		}:
		stdenv.mkDerivation {
			pname = "snd-hdspe";
			version = "1.0.4";

			src = self;

			hardeningDisable = [ "all" ];

			nativeBuildInputs = kernel.moduleBuildDependencies ++ [
				python3
			];

			makeFlags = [
				"KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
			];

			installPhase = ''
				runHook preInstall
				install -D snd-hdspe.ko $out/lib/modules/${kernel.modDirVersion}/extra/snd-hdspe.ko
				runHook postInstall
			'';

			meta = with lib; {
				description = "ALSA driver for RME HDSPe MADI/AES/RayDAT/AIO/AIO Pro cards";
				homepage = "https://github.com/Schroedingers-Cat/snd-hdspe";
				license = licenses.gpl2;
				platforms = platforms.linux;
			};              
		};
	in {

		packages.${system}.default = pkgs.linuxPackages.callPackage snd-hdspe {};

		nixosModules.default = { config, lib, ... }: {
			
			options.snd-hdspe.enable = lib.mkEnableOption "RME HDSPe snd-hdspe driver";

			config = lib.mkIf config.snd-hdspe.enable {
				boot.extraModulePackages = [
					(config.boot.kernelPackages.callPackage snd-hdspe {})
				];

				# The snd-hdspe driver conflicts with snd_hdspm drivers.
				boot.blacklistedKernelModules = [ "snd_hdspm" ];
			};
		};
	};
}

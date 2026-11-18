defmodule Mix.Tasks.Ticwatch.FetchMotorolaFirmware do
  @shortdoc "Adds Venus video firmware to the app's rootfs overlay"

  @moduledoc """
  Adds firmware for Venus, the SoC's video codec, to the app's rootfs
  overlay, so the next `mix firmware` builds it in.

      MIX_TARGET=ticwatch_pro3 mix ticwatch.fetch_motorola_firmware

  The watch ships no Venus firmware, and none is redistributable, so this
  system carries none: without it the watch has no hardware video decoder,
  and `/dev/video*` never appears. Its TrustZone does run another msm8937
  phone's, the Moto G5S's (VIDEO.VE_ULT.3.1). This task downloads those
  files from a public dump of that phone's firmware, at a pinned commit,
  checks each against its SHA-256 and writes them to `lib/firmware` in the
  overlay (`config :nerves, :firmware, rootfs_overlay:`, the first one if
  there are several), where the kernel finds them. Files already there
  with the right hash aren't downloaded again.

  The files are Qualcomm's and Motorola's, under no license that allows
  redistributing them, so a firmware image built with them shouldn't be
  passed on, and the files shouldn't be committed: the task warns if git
  doesn't ignore them. It asks before downloading.

  Files in `/root/firmware` on the watch, which `ticwatch-bringup` stages
  at boot, take precedence over these.

  ## Options

    * `--overlay` - the rootfs overlay to write to, instead of the app's.
    * `--yes` - don't ask before downloading.
  """

  use Mix.Task

  @source "https://gitlab.com/DolphinChips/firmware-motorola-montana"
  @commit "6d448297f0445b029ca450c092743aaba821e6ce"

  @files [
    {"venus.mdt", "426cbe526dec34ae25c7e20ed378ad92958c0bb4bc9d9eb6d2799d60503edca2"},
    {"venus.b00", "b5cefb97bb678330e70f9f00de14ff8d73872da272d977392269945ac71b2b22"},
    {"venus.b01", "4c9c0729bfe733848a4e841a392c0df781c9cfaae19060efb55b8c04a7adf5d4"},
    {"venus.b02", "d6b9de92d426173b655496ccad207661853a9c5f740e96060001e8510cd1453a"},
    {"venus.b03", "f289443b109a29bde0f830156b8744bb80c0604a425f07cca8288e69ea9eaadd"},
    {"venus.b04", "43b09596e1496809aaa964b34c146580f0566c86bc9d7218a1288fd15fb93424"}
  ]

  @switches [overlay: :string, yes: :boolean]

  @impl Mix.Task
  def run(args) do
    {opts, _argv} = OptionParser.parse!(args, strict: @switches)
    dir = Path.join(opts[:overlay] || overlay!(), "lib/firmware")

    unless opts[:yes] || confirm?() do
      Mix.raise("Not fetched")
    end

    fetch(dir)
    warn_unless_ignored(dir)
  end

  defp overlay! do
    Mix.Task.run("loadconfig")

    case Application.get_env(:nerves, :firmware, [])[:rootfs_overlay] do
      overlay when is_binary(overlay) ->
        overlay

      [overlay | _] ->
        overlay

      _ ->
        Mix.raise("No rootfs overlay in config :nerves, :firmware; pass --overlay")
    end
  end

  defp confirm? do
    Mix.shell().yes?(
      """
      This downloads the Moto G5S's Venus firmware from
      #{@source}.
      It is Qualcomm's and Motorola's, and not licensed for redistribution:
      build it only into firmware for watches you own. Continue?\
      """,
      default: :no
    )
  end

  defp fetch(dir) do
    File.mkdir_p!(dir)
    {:ok, _} = Application.ensure_all_started([:inets, :ssl])

    for {name, sha256} <- @files do
      path = Path.join(dir, name)

      unless File.exists?(path) and sha256(File.read!(path)) == sha256 do
        Mix.shell().info("Downloading #{name}")
        body = get!("#{@source}/-/raw/#{@commit}/#{name}")

        if sha256(body) != sha256 do
          Mix.raise("#{name} doesn't match its SHA-256: not using it")
        end

        File.write!(path, body)
      end
    end

    Mix.shell().info("Venus firmware is in #{dir}; mix firmware builds it in.")
  end

  defp get!(url) do
    ssl = [
      verify: :verify_peer,
      cacerts: :public_key.cacerts_get(),
      customize_hostname_check: [match_fun: :public_key.pkix_verify_hostname_match_fun(:https)]
    ]

    case :httpc.request(:get, {String.to_charlist(url), []}, [ssl: ssl, autoredirect: true],
           body_format: :binary
         ) do
      {:ok, {{_, 200, _}, _headers, body}} -> body
      {:ok, {{_, status, _}, _headers, _body}} -> Mix.raise("#{url}: HTTP #{status}")
      {:error, reason} -> Mix.raise("#{url}: #{inspect(reason)}")
    end
  end

  defp sha256(data), do: :crypto.hash(:sha256, data) |> Base.encode16(case: :lower)

  defp warn_unless_ignored(dir) do
    path = Path.join(dir, "venus.mdt")

    case System.cmd("git", ["check-ignore", "-q", path], stderr_to_stdout: true) do
      {_, 1} ->
        Mix.shell().error("""
        git doesn't ignore #{dir}/venus.*, so the firmware could be committed.
        Add to .gitignore: /#{Path.relative_to_cwd(dir)}/venus.*\
        """)

      # Ignored, or not a git repository.
      _ ->
        :ok
    end
  rescue
    # No git.
    ErlangError -> :ok
  end
end

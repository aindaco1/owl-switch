// Opt-in release acceptance; execute with the bundled Deno binary.
const issuers = ["dv-g3b", "dv-g3c", "ov-g3b", "ov-g3c", "ev-g3b", "ev-g3c"];
const control = await fetch("https://example.com/", {signal: AbortSignal.timeout(15000)});
if (control.status !== 200) throw new Error("valid HTTPS control failed");
await control.body?.cancel();
console.log("CONTROL example.com accepted");
for (const issuer of issuers) {
  for (const cross of ["", "-aaa"]) {
    const host = `${issuer}${cross}-emudhra.we-have-a-problem.com`;
    let failure;
    try {
      const response = await fetch(`https://${host}/`, {redirect: "error", signal: AbortSignal.timeout(15000)});
      await response.body?.cancel();
    } catch (error) { failure = error; }
    if (!String(failure?.cause || failure).includes("CertDistrusted")) {
      throw new Error(`${host}: certificate rejection not established: ${failure || "ACCEPTED"}`);
    }
    console.log(`REJECTED ${host}: ${failure.cause}`);
  }
}


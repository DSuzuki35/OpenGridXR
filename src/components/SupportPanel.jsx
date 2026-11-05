import React, { useState } from "react";
import { ArrowUpRight, Coffee, ExternalLink } from "lucide-react";
import { api } from "../api.mjs";

export default function SupportPanel({ disabled = false }) {
  const [error, setError] = useState("");
  async function open(operation) {
    setError("");
    try { await operation(); }
    catch { setError("Could not open your browser. Please try again."); }
  }
  return (
    <div className="sidebar-support" role="region" aria-label="Optional support">
      <button className="help-button" aria-label="Buy me a coffee" title="Buy me a coffee" disabled={disabled} onClick={() => open(() => api.openBuyMeACoffee())}>
        <Coffee size={16} /><span>Buy me a coffee</span><ArrowUpRight size={13} />
      </button>
      <button className="help-button" aria-label="Affiliate links" title="Affiliate links (purchases may earn Dan a commission)" disabled={disabled} onClick={() => open(() => api.openAffiliateLinks())}>
        <ExternalLink size={16} /><span>Affiliate links</span><ArrowUpRight size={13} />
      </button>
      <p>Optional support. Affiliate purchases may earn Dan a commission.</p>
      {error && <p role="alert">{error}</p>}
    </div>
  );
}

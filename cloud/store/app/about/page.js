import Link from "next/link";
import {Icon} from "../components";

export const metadata={title:"关于 · Ouse 应用商店"};

export default function About(){
 return <main className="browse-page about-page"><section className="hero"><div className="hero-copy"><span className="eyebrow"><span/>POCKET POSSIBILITIES</span><h1>小小口袋，<br/>装下更多可能<span>。</span></h1><p>发现实用、有趣的应用，让你的 OusePocket 更顺手。</p><Link href="/" className="hero-link">探索应用<Icon name="arrow" size={18}/></Link></div><div className="hero-art" aria-hidden="true"><div className="orbit orbit-one"/><div className="orbit orbit-two"/><div className="hero-device"><div className="device-notch"/><span className="device-time">OusePocket</span><div className="device-icons">{["download","code","spark","box"].map(name=><span className="device-placeholder" key={name}><Icon name={name}/></span>)}</div><div className="device-line"/></div><span className="float-spark"><Icon name="spark" size={26}/></span></div></section></main>;
}

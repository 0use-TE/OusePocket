import "./style.css";
import Shell from "./shell";
export const metadata={title:"Ouse 应用商店",description:"OusePocket 应用发布与下载"};
export default function Layout({children}){return <html lang="zh-CN"><body><Shell>{children}</Shell></body></html>;}

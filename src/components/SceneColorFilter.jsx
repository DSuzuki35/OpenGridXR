
// The same normalized sRGB operation order as gradeColorRgb/native shading.
// Keep artwork outside this filter; alpha is preserved at every step.
export default function SceneColorFilter({id,preset}) {
  const brightness=preset.brightness/100,contrast=preset.contrast/100,saturation=preset.saturation/100;
  const slope=brightness*contrast,intercept=.5*(1-contrast),inverse=1-saturation;
  const row=(luma,index)=>[0,1,2].map(channel=>luma[channel]*inverse+(index===channel?saturation:0)).concat([0,0]);
  const luma=[.2126,.7152,.0722];
  const matrix=[...row(luma,0),...row(luma,1),...row(luma,2),0,0,0,1,0].join(' ');
  return <filter id={id} colorInterpolationFilters="sRGB">
    <feComponentTransfer><feFuncR type="linear" slope={slope} intercept={intercept}/><feFuncG type="linear" slope={slope} intercept={intercept}/><feFuncB type="linear" slope={slope} intercept={intercept}/><feFuncA type="identity"/></feComponentTransfer>
    <feComponentTransfer><feFuncR type="gamma" amplitude="1" exponent={100/preset.gamma} offset="0"/><feFuncG type="gamma" amplitude="1" exponent={100/preset.gamma} offset="0"/><feFuncB type="gamma" amplitude="1" exponent={100/preset.gamma} offset="0"/><feFuncA type="identity"/></feComponentTransfer>
    <feColorMatrix type="matrix" values={matrix}/>
  </filter>;
}

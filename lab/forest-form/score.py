"""Compare five separately visible photographed crowns with 16 projected game crowns."""
import argparse
import json
from pathlib import Path
from statistics import median
from PIL import Image, ImageChops, ImageDraw

# The reference copies are resized to width 800, aspect preserved. An exposed
# tip and signed offsets to visually interpretable outline edges on two rows.
# Crowded, inseparable tops were left out. Values are descriptive field readings.
MARKS={
 "2025":[(46,136,(-5,6),(-6,9)),(185,234,(-4,6),(-5,8)),(237,248,(-12,15),(-13,17))],
 "2020":[(572,172,(-5,5),(-6,7)),(739,158,(-4,5),(-7,11))],
}


def measure(render):
    image=Image.open(render).convert("RGB")
    result=[]
    for i in range(16):
        left=i%4*240;top=i//4*160
        crop=image.crop((left,top,left+240,top+160))
        widths=[]
        for y in range(160):
            row=[x for x in range(240) if min(crop.getpixel((x,y)))<200]
            widths.append(max(row)-min(row)+1 if row else 0)
        y=next(y for y in range(5,100) if min(widths[y:y+3])>=2)
        result.append([median(widths[y+d-1:y+d+2])/d for d in (12,20)])
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("render_prefix",type=Path);parser.add_argument("photo_2025",type=Path)
    parser.add_argument("photo_2020",type=Path);parser.add_argument("out",type=Path)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    p=lambda n:Path(str(a.render_prefix)+n)
    ref={}
    for k,source in zip(MARKS,[a.photo_2025,a.photo_2020]):
        im=Image.open(source).convert("RGB").resize((800,533),Image.Resampling.LANCZOS)
        draw=ImageDraw.Draw(im)
        readings=[]
        for x,y,mid,low in MARKS[k]:
            draw.line((x-3,y,x+3,y),fill="cyan")
            for depth,(left,right) in zip((12,20),(mid,low)):
                draw.line((x+left,y+depth,x+right,y+depth),fill="#ff32b1")
                readings.append((right-left)/depth)
        ref[k]=readings
        im.save(a.out/f"reference-widths-{k}.webp",quality=91)
    result={"definition":"projected width divided by vertical drop from visible crown apex at 12 and 20 pixels",
            "photoWidthsOverDrop":ref,"photoMedians":{k:median(v) for k,v in ref.items()},
            "photoPooledMedian":median(sum(ref.values(),[]))}
    for k in ("old","new"):
        rows=measure(p("-profiles-"+k+".png"))
        result[k]={"individuals":rows,"pooledMedian":median(sum(rows,[]))}
    im=Image.open(p("-off.png")).convert("RGB")
    for k in ("on","off-repeat"):
        diff=ImageChops.difference(im,Image.open(p("-"+k+".png")).convert("RGB"))
        result[k]={"pixelsOver4":sum(max(px)>4 for px in diff.get_flattened_data()),"bbox":diff.getbbox()}
    scene=json.loads(p("-geometry.json").read_text())
    result["loadedCrowns"]=sum(t["trees"] for t in scene["tiles"].values())
    result["eligible"]=sum(t["gated"] for t in scene["tiles"].values())
    result["inHorizontalViewingCone"]=len(scene["visible"])
    result["sourceAndRenderedTreesAreIndividuallyMatched"]=False
    result["upperOutlineReadingUncertaintyPixels"]=2
    (a.out/"shape-score.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ["photoMedians","photoPooledMedian","on","off-repeat","eligible","loadedCrowns"]}))
    print("rendered pooled medians",result["old"]["pooledMedian"],result["new"]["pooledMedian"])


if __name__=="__main__":main()

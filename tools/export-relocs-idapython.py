# Run inside IDA on the private StarCraft.exe database.
# Conservative: offset operands in code + data xrefs; require exact original dword.
import ida_bytes, ida_xref, ida_segment, ida_ua, idautils, idc, collections
segments=[]
for s in idautils.Segments():
    q=ida_segment.getseg(s)
    segments.append((q.start_ea,q.end_ea))
rows={}
def inside(value, width=1):
    for a,b in segments:
        if a <= value and value+width <= b:
            return True
    return False
for s in idautils.Segments():
    seg=ida_segment.getseg(s)
    for ea in idautils.Heads(s,seg.end_ea):
        for xr in idautils.XrefsFrom(ea,0):
            if xr.type not in (ida_xref.dr_O,ida_xref.dr_W,ida_xref.dr_R):
                continue
            src=xr.frm
            if inside(src,4) and inside(xr.to) and ida_bytes.get_dword(src)==xr.to:
                rows[(src,xr.to,'data-xref')]=True
        if not ida_bytes.is_code(ida_bytes.get_full_flags(ea)):
            continue
        for i in range(8):
            fl=ida_bytes.get_full_flags(ea)
            if not fl:
                break
            insn=ida_ua.insn_t()
            if ida_ua.decode_insn(insn,ea) == 0:
                break
            operand=insn.ops[i]
            off=operand.offb
            if off <= 0 or off+4 > insn.size:
                continue
            src=ea+off
            op=ida_bytes.get_dword(src)
            if inside(src,4) and inside(op) and op==idc.get_operand_value(ea,i):
                has_xref=False
                for xr in idautils.XrefsFrom(ea,0):
                    if xr.to==op:
                        has_xref=True
                        break
                if has_xref:
                    rows[(src,op,'code-offset')]=True
out='/home/enne2/dev/starcraft-armv6/work/relocs/relocations.csv'
with open(out,'w') as f:
    for src,target,kind in sorted(rows):
        f.write('0x%x,0x%x,\n' % (src,target))
print({'rows':len(rows),'classes':dict(collections.Counter(x[2] for x in rows)),'output':out})

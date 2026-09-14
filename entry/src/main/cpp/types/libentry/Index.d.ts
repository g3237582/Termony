export const run: () => string | undefined;
export const send: (content: ArrayBuffer) => void;
export const createSurface: (id: BigInt) => void;
export const destroySurface: (id: BigInt) => void;
export const resizeSurface: (id: BigInt, width: number, height: number) => void;
export const scroll: (offset: number) => void;
// poll if any thing to copy/paste
export const checkCopy: () => string | undefined;
export const checkPaste: () => boolean;
// send paste result
export const pushPaste: (base64: string) => void;
//
export const onForeground: () => void;
export const onBackground: () => void;
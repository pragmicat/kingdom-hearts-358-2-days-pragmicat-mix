// Keep mod feedback alongside the display without covering either game screen.
export function attachDaysNotices(module, canvas) {
    const document = canvas.ownerDocument;
    const region = document.createElement('div');
    region.setAttribute('role', 'status');
    region.setAttribute('aria-label', 'Game notices');
    region.setAttribute('aria-live', 'polite');
    region.setAttribute('aria-atomic', 'false');
    region.style.cssText = 'display:block;min-height:1.4em;max-width:100%;overflow-wrap:anywhere;font:500 1rem/1.4 system-ui,sans-serif';
    const lines = [false, true].map(warning => {
        const line = document.createElement('div');
        line.style.cssText = 'background:#121823;color:#f5f6f8;padding:.35em .65em;border-inline-start:3px solid ' +
            (warning ? '#efbb6d' : '#90bbdf');
        line.hidden = true;
        region.append(line);
        return line;
    });
    canvas.after(region);
    const timers = [];
    function show(warning, text) {
        const index = warning ? 1 : 0;
        const line = lines[index];
        clearTimeout(timers[index]);
        line.hidden = false;
        line.textContent = text;
        timers[index] = setTimeout(() => {
            line.hidden = true;
            line.textContent = '';
        }, 5000);
    }
    module.onDaysModNotice = show;
    return {
        dispose() {
            timers.forEach(clearTimeout);
            if (module.onDaysModNotice === show) delete module.onDaysModNotice;
            region.remove();
        },
    };
}

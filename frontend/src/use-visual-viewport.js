import { useEffect } from 'preact/hooks';

/**
 * Tracks the visual viewport height and sets --app-height CSS custom property.
 *
 * On mobile, when the virtual keyboard opens, the visual viewport shrinks.
 * By setting --app-height to visualViewport.height, the app container
 * adjusts to the visible area, keeping the input field above the keyboard.
 *
 * Falls back to 100vh when visualViewport is unavailable or the hook unmounts.
 */
export function useVisualViewport() {
  useEffect(() => {
    const vv = window.visualViewport;
    if (!vv) return;

    function update() {
      document.documentElement.style.setProperty('--app-height', `${vv.height}px`);
    }

    update();
    vv.addEventListener('resize', update);

    return () => {
      vv.removeEventListener('resize', update);
      document.documentElement.style.removeProperty('--app-height');
    };
  }, []);
}

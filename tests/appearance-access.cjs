exports.revealAppearance = async page => {
  await page.getByRole('navigation', {name:'Main navigation'}).waitFor();
  await page.keyboard.press('Control+Alt+Shift+KeyH');
};

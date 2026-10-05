import logo from '../../../src/images/sources/rickyos/approved-logo.png';

document.querySelectorAll('[data-brand-logo]').forEach(img => { img.src = logo; });

// Preserve bookmarked entry points without loading serial code on the homepage.
// Relative URLs work under GitHub Pages project paths and custom domains.
if (document.body.classList.contains('concept-a')) {
  const query = new URLSearchParams(location.search);
  if (query.get('demo') === '1') location.replace('./install.html?demo=1');
  else if (location.hash === '#install') location.replace('./install.html');
}

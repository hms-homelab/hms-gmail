import { Component } from '@angular/core';
import { RouterOutlet, RouterLink, RouterLinkActive } from '@angular/router';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [RouterOutlet, RouterLink, RouterLinkActive],
  template: `
    <nav class="nav-bar">
      <span class="nav-title">HMS Gmail</span>
      <a routerLink="/search" routerLinkActive="active">Search</a>
      <a routerLink="/backup" routerLinkActive="active">Backup</a>
    </nav>
    <main>
      <router-outlet />
    </main>
  `,
  styles: [`:host { display: block; min-height: 100vh; } main { max-width: 960px; margin: 0 auto; }`]
})
export class AppComponent {}
